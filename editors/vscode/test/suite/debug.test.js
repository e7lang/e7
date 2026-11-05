// Integration tests of the E7 debugger: real debug sessions in VS Code, observed through a debug
// adapter tracker (every DAP message the adapter sends) and driven with customRequest.
const assert = require("assert");
const path = require("path");
const vscode = require("vscode");

const root = () => vscode.workspace.workspaceFolders[0].uri.fsPath;
const fixture = (f) => path.join(root(), "debug", f);
const MAIN = () => fixture("main.e7");
const GEO = () => fixture(path.join("lib", "geo.e7"));

// session id -> { messages, cursor per event name }
const logs = new Map();
const logOf = (s) => {
  if (!logs.has(s.id)) logs.set(s.id, { messages: [], seen: new Set() });
  return logs.get(s.id);
};

async function waitFor(fn, what, timeout = 30000) {
  const t0 = Date.now();
  for (;;) {
    const v = await fn();
    if (v) return v;
    if (Date.now() - t0 > timeout) throw new Error(`timed out waiting for ${what}`);
    await new Promise((r) => setTimeout(r, 50));
  }
}

/** The next DAP event `name` of a session that was not returned before. */
function nextEvent(session, name, pred = () => true) {
  const log = logOf(session);
  return waitFor(() => {
    const m = log.messages.find((x) => x.type === "event" && x.event === name && !log.seen.has(x) && pred(x));
    if (m) log.seen.add(m);
    return m;
  }, `${name} event`);
}
const outputs = (session) => logOf(session).messages.filter((m) => m.type === "event" && m.event === "output").map((m) => m.body);

async function start(config = {}) {
  const started = new Promise((resolve) => {
    const d = vscode.debug.onDidStartDebugSession((s) => {
      if (s.type === "e7") {
        d.dispose();
        resolve(s);
      }
    });
  });
  const ok = await vscode.debug.startDebugging(vscode.workspace.workspaceFolders[0], {
    type: "e7",
    request: "launch",
    name: "E7 test",
    program: MAIN(),
    ...config,
  });
  assert.ok(ok, "debug session started");
  return started;
}

async function stopped(session, reason) {
  const ev = await nextEvent(session, "stopped");
  if (reason) assert.strictEqual(ev.body.reason, reason, JSON.stringify(ev.body));
  return ev.body;
}

const stack = async (s) => (await s.customRequest("stackTrace", { threadId: 1 })).stackFrames;
async function locals(s, frameId) {
  const { scopes } = await s.customRequest("scopes", { frameId });
  const loc = scopes.find((x) => x.name === "Locals");
  return children(s, loc.variablesReference);
}
async function children(s, ref, extra = {}) {
  const { variables } = await s.customRequest("variables", { variablesReference: ref, ...extra });
  return Object.fromEntries(variables.map((v) => [v.name, v]));
}

// ids of the sessions that have ended (recorded from before() on)
const ended = new Set();

async function end(session) {
  if (ended.has(session.id)) return;
  await vscode.debug.stopDebugging(session);
  await waitFor(() => ended.has(session.id), "end of the debug session");
}

const terminatedSoon = (session) => waitFor(() => ended.has(session.id), "end of the debug session");

describe("E7 debugger", function () {
  this.timeout(90000);
  let tracker;
  let onEnd;

  before(async () => {
    const ext = vscode.extensions.getExtension("e7lang.e7");
    await ext.activate();
    onEnd = vscode.debug.onDidTerminateDebugSession((s) => ended.add(s.id));
    tracker = vscode.debug.registerDebugAdapterTrackerFactory("e7", {
      createDebugAdapterTracker: (session) => ({ onDidSendMessage: (m) => logOf(session).messages.push(m) }),
    });
  });
  after(() => {
    tracker.dispose();
    onEnd.dispose();
  });
  afterEach(async () => {
    vscode.debug.removeBreakpoints(vscode.debug.breakpoints);
    const s = vscode.debug.activeDebugSession;
    if (s && !ended.has(s.id)) await end(s);
  });

  const bp = (file, line) => new vscode.SourceBreakpoint(new vscode.Location(vscode.Uri.file(file), new vscode.Position(line - 1, 0)));

  it("contributes the e7 debugger and the Debug File command", async () => {
    const pkg = vscode.extensions.getExtension("e7lang.e7").packageJSON;
    assert.strictEqual(pkg.contributes.debuggers[0].type, "e7");
    assert.ok((await vscode.commands.getCommands(true)).includes("e7.debugFile"));
  });

  it("stops at a breakpoint with the right line and stack, steps over / out / in", async () => {
    vscode.debug.addBreakpoints([bp(MAIN(), 7)]);
    const s = await start();
    let ev = await stopped(s, "breakpoint");
    assert.ok(ev.hitBreakpointIds.length === 1);
    let frames = await stack(s);
    assert.deepStrictEqual(frames.map((f) => [f.name, f.line]), [["add", 7], ["main", 27]]);
    assert.strictEqual(frames[0].source.path, MAIN());
    assert.strictEqual(frames[1].column, 11);
    assert.deepStrictEqual(Object.keys(await locals(s, frames[0].id)), ["a", "b", "s"]);

    await s.customRequest("next", { threadId: 1 });
    await stopped(s, "step");
    assert.strictEqual((await stack(s))[0].line, 8);

    await s.customRequest("stepOut", { threadId: 1 });
    await stopped(s, "step");
    frames = await stack(s);
    assert.deepStrictEqual([frames[0].name, frames[0].line], ["main", 28]);

    await s.customRequest("stepIn", { threadId: 1 });
    await stopped(s, "step");
    frames = await stack(s);
    assert.deepStrictEqual(frames.map((f) => [f.name, f.line]), [["Point.norm", 9], ["main", 28]]);
    assert.strictEqual(frames[0].source.path, GEO(), "frames of imported files map to workspace files");

    await s.customRequest("stepOut", { threadId: 1 });
    await stopped(s, "step");
    await s.customRequest("continue", { threadId: 1 });
    ev = await stopped(s, "breakpoint"); // the second add() call
    assert.strictEqual((await stack(s))[0].line, 7);
    await end(s);
  });

  it("verifies breakpoints in imported files", async () => {
    vscode.debug.addBreakpoints([bp(GEO(), 9), bp(MAIN(), 1000)]);
    const s = await start();
    const changed = await nextEvent(s, "breakpoint", (m) => m.body.breakpoint.verified);
    assert.strictEqual(changed.body.breakpoint.line, 9);
    await stopped(s, "breakpoint");
    const frames = await stack(s);
    assert.strictEqual(frames[0].source.path, GEO());
    assert.strictEqual(frames[0].name, "Point.norm");
    await end(s);
  });

  it("debugger statement: variables (nested objects, arrays, Map, closure), evaluate", async () => {
    const s = await start();
    const ev = await stopped(s, "debugger statement");
    assert.ok(ev);
    const [top] = await stack(s);
    assert.strictEqual(top.line, 29);
    const loc = await locals(s, top.id);
    assert.strictEqual(loc.r.value, "8");
    assert.strictEqual(loc.p.type, "Point");
    const p = await children(s, loc.p.variablesReference);
    assert.deepStrictEqual([p.x.value, p.y.value], ["3", "4"]);

    assert.strictEqual(loc.m.type, "Map");
    const m = Object.values(await children(s, loc.m.variablesReference));
    assert.deepStrictEqual(m.map((v) => v.value), ["1"]);

    const nested = await children(s, loc.nested.variablesReference);
    assert.strictEqual(nested.name.value, "'n'");
    const inner = await children(s, nested.inner.variablesReference);
    assert.strictEqual(inner.list.type, "Array");
    assert.strictEqual(inner.list.indexedVariables, 3);
    const page = await children(s, inner.list.variablesReference, { filter: "indexed", start: 1, count: 2 });
    assert.deepStrictEqual(Object.entries(page).map(([k, v]) => [k, v.value]), [["1", "2"], ["2", "3"]]);

    assert.strictEqual(loc.counter.type, "function");
    const captured = await children(s, loc.counter.variablesReference);
    assert.strictEqual(captured.n.value, "11");

    const { scopes } = await s.customRequest("scopes", { frameId: top.id });
    const globals = await children(s, scopes.find((x) => x.name === "Globals").variablesReference);
    assert.strictEqual(globals.calls.value, "2");

    const watch = await s.customRequest("evaluate", { expression: "nested.inner.list.length", frameId: top.id, context: "watch" });
    assert.strictEqual(watch.result, "3");
    const hover = await s.customRequest("evaluate", { expression: "p", frameId: top.id, context: "hover" });
    assert.strictEqual(hover.result, "Point { x: 3, y: 4 }");
    assert.ok(hover.variablesReference > 0);
    const repl = await s.customRequest("evaluate", { expression: "p.x", frameId: top.id, context: "repl" });
    assert.strictEqual(repl.result, "3");
    await assert.rejects(s.customRequest("evaluate", { expression: "nope", frameId: top.id, context: "repl" }), /nope/);

    const gone = terminatedSoon(s);
    await s.customRequest("continue", { threadId: 1 });
    await nextEvent(s, "exited");
    await gone;
    const out = outputs(s).map((o) => o.output);
    assert.ok(out.includes("top-level start\n"), JSON.stringify(out));
    assert.ok(out.includes("r = 8\n"), JSON.stringify(out));
    assert.ok(out.includes("main() returned 28\n"), JSON.stringify(out));
  });

  it("stops on a runtime error with its message", async () => {
    const s = await start({ call: "fail" });
    const ev = await stopped(s, "exception");
    assert.match(ev.text, /index 5 out of range/);
    assert.strictEqual((await stack(s))[0].line, 44);
    const info = await s.customRequest("exceptionInfo", { threadId: 1 });
    assert.match(info.description, /out of range/);
    const gone = terminatedSoon(s);
    await s.customRequest("continue", { threadId: 1 });
    const exited = await nextEvent(s, "exited");
    assert.strictEqual(exited.body.exitCode, 2);
    await gone;
    assert.ok(outputs(s).some((o) => o.category === "stderr" && /at fail \(main\.e7:44/.test(o.output)));
  });

  it("calls an exported function with arguments; output events; stop on entry", async () => {
    const s = await start({ call: "sum", args: [4, "t="], stopOnEntry: true });
    const ev = await stopped(s, "entry");
    assert.ok(ev);
    assert.strictEqual((await stack(s))[0].name, "<init>");
    const gone = terminatedSoon(s);
    await s.customRequest("continue", { threadId: 1 });
    await gone;
    const out = outputs(s);
    assert.ok(out.some((o) => o.category === "stdout" && o.output === "top-level start\n"));
    assert.ok(out.some((o) => o.output === 'sum() returned "t=6"\n'), JSON.stringify(out));
  });

  it("pauses an infinite loop and terminates it", async () => {
    const s = await start({ call: "spin" });
    await new Promise((r) => setTimeout(r, 500));
    await s.customRequest("pause", { threadId: 1 });
    await stopped(s, "pause");
    const [top] = await stack(s);
    assert.strictEqual(top.name, "spin");
    assert.ok(top.line >= 47 && top.line <= 51, String(top.line));
    assert.ok(Number((await locals(s, top.id)).i.value) > 0);
    await s.customRequest("continue", { threadId: 1 });
    await new Promise((r) => setTimeout(r, 200));
    await end(s);
    await nextEvent(s, "terminated");
  });

  it("pauses an infinite loop in top-level code", async () => {
    const s = await start({ program: fixture("loop.e7") });
    await new Promise((r) => setTimeout(r, 500));
    await s.customRequest("pause", { threadId: 1 });
    await stopped(s, "pause");
    const [top] = await stack(s);
    assert.strictEqual(top.name, "<init>");
    assert.strictEqual(top.source.path, fixture("loop.e7"));
    await end(s);
  });

  it("E7: Debug File and a configuration without program debug the active file", async () => {
    let started = new Promise((r) => {
      const d = vscode.debug.onDidStartDebugSession((x) => (d.dispose(), r(x)));
    });
    assert.strictEqual(await vscode.commands.executeCommand("e7.debugFile", vscode.Uri.file(MAIN())), true);
    let s = await started;
    await stopped(s, "debugger statement");
    await end(s);

    const doc = await vscode.workspace.openTextDocument(vscode.Uri.file(MAIN()));
    await vscode.window.showTextDocument(doc);
    started = new Promise((r) => {
      const d = vscode.debug.onDidStartDebugSession((x) => (d.dispose(), r(x)));
    });
    assert.ok(await vscode.debug.startDebugging(undefined, { type: "e7", request: "launch", name: "no program" }));
    s = await started;
    await stopped(s, "debugger statement");
    assert.strictEqual((await stack(s))[0].source.path, MAIN());
    await end(s);
  });
});
