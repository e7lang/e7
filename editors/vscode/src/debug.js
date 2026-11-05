// E7 debugger for VS Code: a Debug Adapter Protocol implementation that runs inline in the extension
// host (DebugAdapterInlineImplementation) and drives an @e7lang/e7 debug session in a worker thread
// (src/debugWorker.js). See docs/DEBUGGING.md.
const vscode = require("vscode");
const path = require("path");
const fs = require("fs");
const { Worker } = require("worker_threads");
const {
  DebugSession,
  InitializedEvent,
  StoppedEvent,
  ContinuedEvent,
  OutputEvent,
  TerminatedEvent,
  ExitedEvent,
  BreakpointEvent,
  Thread,
  Source,
} = require("@vscode/debugadapter");

const TYPE = "e7";
const THREAD_ID = 1;
const ERROR_FILTER = "runtimeErrors";
const INDEXED_TYPES = new Set(["Array", "tuple", "Uint8Array", "Set"]);

/** Request/response channel to the worker thread. */
class WorkerClient {
  constructor(wasmPath, onEvent) {
    this.worker = new Worker(path.join(__dirname, "debugWorker.js"), { workerData: { wasmPath } });
    this.pending = new Map();
    this.seq = 0;
    this.exited = false;
    this.worker.on("message", (msg) => {
      if (msg.event) return onEvent(msg.event, msg.body);
      const p = this.pending.get(msg.id);
      if (!p) return;
      this.pending.delete(msg.id);
      if (msg.error) {
        const err = new Error(msg.error.message);
        err.diagnostics = msg.error.diagnostics;
        p.reject(err);
      } else p.resolve(msg.result);
    });
    const fail = (e) => {
      this.exited = true;
      for (const p of this.pending.values()) p.reject(e);
      this.pending.clear();
    };
    this.worker.on("error", (e) => {
      fail(e);
      onEvent("crashed", String(e?.message ?? e));
    });
    this.worker.on("exit", () => fail(new Error("the E7 debugger worker has exited")));
  }

  call(method, params) {
    if (this.exited) return Promise.reject(new Error("the E7 debugger worker has exited"));
    const id = ++this.seq;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.worker.postMessage({ id, method, params });
    });
  }

  /** Asks the session to terminate; kills the worker if it does not answer quickly (e.g. inside a long native call). */
  async dispose() {
    if (this.exited) return;
    try {
      await Promise.race([this.call("terminate"), new Promise((r) => setTimeout(r, 500))]);
    } catch {
      // ignore: the worker is killed below
    }
    this.exited = true;
    await this.worker.terminate();
  }
}

// UTF-8 byte column (1-based) -> UTF-16 column (1-based) on a source line
function utf16Column(lineText, col) {
  if (typeof lineText !== "string" || col <= 1) return Math.max(1, col);
  const bytes = Buffer.from(lineText, "utf8");
  return bytes.subarray(0, Math.min(col - 1, bytes.length)).toString("utf8").length + 1 + Math.max(0, col - 1 - bytes.length);
}

class E7DebugSession extends DebugSession {
  constructor(context) {
    super();
    this.context = context;
    this.setDebuggerLinesStartAt1(true);
    this.setDebuggerColumnsStartAt1(true);
    this.client = null;
    this.programDir = "";
    this.configured = null;
    this.configurationDone = new Promise((r) => (this.configured = r));
    this.breakOnError = true;
    this.lastStop = null;
    this.breakpointState = new Map(); // id -> "line:verified"
    this.lineCache = new Map();
    this.ended = false;
  }

  shutdown() {}

  initializeRequest(response) {
    response.body = {
      supportsConfigurationDoneRequest: true,
      supportsEvaluateForHovers: true,
      supportsTerminateRequest: true,
      supportsExceptionInfoRequest: true,
      supportsDelayedStackTraceLoading: false,
      supportsSingleThreadExecutionRequests: false,
      exceptionBreakpointFilters: [
        { filter: ERROR_FILTER, label: "Runtime errors", description: "Stop when a runtime error is about to terminate the script", default: true },
      ],
    };
    this.sendResponse(response);
  }

  // ---------------------------------------------------------------- paths
  toVmPath(file) {
    const rel = path.relative(this.programDir, file);
    return (path.isAbsolute(rel) ? file : rel).split(path.sep).join("/");
  }
  toClientPath(vmFile) {
    return path.resolve(this.programDir, vmFile.split("/").join(path.sep));
  }
  source(vmFile) {
    const abs = this.toClientPath(vmFile);
    return new Source(path.basename(abs), abs);
  }
  lineText(abs, line) {
    let lines = this.lineCache.get(abs);
    if (!lines) {
      const doc = vscode.workspace.textDocuments.find((d) => d.uri.scheme === "file" && d.uri.fsPath === abs);
      let text = doc ? doc.getText() : undefined;
      if (text === undefined) {
        try {
          text = fs.readFileSync(abs, "utf8");
        } catch {
          text = "";
        }
      }
      lines = text.split(/\r?\n/);
      this.lineCache.set(abs, lines);
    }
    return lines[line - 1];
  }

  // ---------------------------------------------------------------- events from the worker
  onWorkerEvent(event, body) {
    if (this.ended) return;
    if (event === "output") {
      this.sendEvent(new OutputEvent(body + "\n", "stdout"));
    } else if (event === "stopped") {
      this.lastStop = body;
      this.lineCache.clear();
      const reason = body.reason === "debugger" ? "debugger statement" : body.reason;
      const ev = new StoppedEvent(reason, THREAD_ID, body.reason === "exception" ? body.message : undefined);
      ev.body.allThreadsStopped = true;
      if (body.breakpoints?.length) ev.body.hitBreakpointIds = body.breakpoints;
      if (body.reason === "exception") ev.body.description = "Paused on runtime error";
      this.sendEvent(ev);
    } else if (event === "terminated") {
      this.finish(body);
    } else if (event === "crashed") {
      this.sendEvent(new OutputEvent(`E7 debugger: ${body}\n`, "stderr"));
      this.end(1);
    }
  }

  finish(t) {
    if (this.ended) return;
    if (t.status === "ok") {
      if (t.result !== undefined) this.sendEvent(new OutputEvent(`${this.callName}() returned ${show(t.result)}\n`, "console"));
    } else if (t.status !== "aborted" && t.error) {
      this.sendEvent(new OutputEvent(`${t.error}\n`, "stderr"));
    }
    this.end(t.status === "ok" ? 0 : t.status === "aborted" ? 0 : t.code || 1);
  }

  end(exitCode) {
    if (this.ended) return;
    this.ended = true;
    this.sendEvent(new ExitedEvent(exitCode));
    this.sendEvent(new TerminatedEvent());
    const c = this.client;
    this.client = null;
    if (c) c.dispose();
    if (this.onEnd) this.onEnd();
  }

  static bpKey(b) {
    return b.verified ? `${b.line}` : "unverified";
  }

  updateBreakpoints(list) {
    for (const b of list || []) {
      const key = E7DebugSession.bpKey(b);
      if (this.breakpointState.get(b.id) === key) continue;
      this.breakpointState.set(b.id, key);
      const bp = b.verified ? { id: b.id, verified: true, line: b.line } : { id: b.id, verified: false, message: "No E7 code at this line" };
      this.sendEvent(new BreakpointEvent("changed", bp));
    }
  }

  // ---------------------------------------------------------------- launch
  async launchRequest(response, args) {
    const program = args.program;
    if (!program || !fs.existsSync(program)) {
      return this.sendErrorResponse(response, 1001, `E7: program not found: ${program || "(none)"}`);
    }
    this.programDir = path.dirname(program);
    this.callName = args.call || "main";
    const overlay = {};
    for (const d of vscode.workspace.textDocuments) if (d.uri.scheme === "file" && d.isDirty) overlay[d.uri.fsPath] = d.getText();
    const wasmPath = path.join(__dirname, "e7wasm.wasm");
    this.client = new WorkerClient(wasmPath, (e, b) => this.onWorkerEvent(e, b));
    try {
      await this.client.call("create", {
        program,
        overlay,
        call: args.call || "",
        args: Array.isArray(args.args) ? args.args : [],
        memoryLimit: args.memoryLimit,
        budget: args.budget,
        breakOnError: this.breakOnError,
      });
    } catch (e) {
      const text = e.diagnostics?.length
        ? e.diagnostics.map((d) => `${this.toClientPath(d.file)}:${d.line}:${d.column}: error: ${d.message}`).join("\n")
        : e.message;
      this.sendEvent(new OutputEvent(text + "\n", "stderr"));
      this.client?.dispose();
      this.client = null;
      return this.sendErrorResponse(response, 1002, `E7: cannot debug ${path.basename(program)}: ${e.diagnostics?.length ? `${e.diagnostics.length} compile error(s)` : e.message}`);
    }
    // now that the session exists, VS Code may send breakpoints and exception filters
    this.sendEvent(new InitializedEvent());
    await Promise.race([this.configurationDone, new Promise((r) => setTimeout(r, 5000))]);
    if (!this.client) return this.sendResponse(response);
    this.sendEvent(new OutputEvent(`Debugging ${path.basename(program)}${args.call ? ` (${args.call})` : ""}\n`, "console"));
    try {
      const bps = await this.client.call("launch", { stopOnEntry: !!args.stopOnEntry });
      this.updateBreakpoints(bps);
      this.sendResponse(response);
    } catch (e) {
      this.sendErrorResponse(response, 1003, `E7: ${e.message}`);
    }
  }

  configurationDoneRequest(response) {
    this.configured();
    this.sendResponse(response);
  }

  async setBreakPointsRequest(response, args) {
    const file = args.source.path;
    const lines = (args.breakpoints || []).map((b) => b.line);
    if (!this.client || !file) {
      response.body = { breakpoints: lines.map((line) => ({ verified: false, line })) };
      return this.sendResponse(response);
    }
    try {
      const list = await this.client.call("setBreakpoints", { file: this.toVmPath(file), lines });
      response.body = {
        breakpoints: list.map((b) => {
          this.breakpointState.set(b.id, E7DebugSession.bpKey(b));
          return {
            id: b.id,
            verified: b.verified,
            line: b.verified ? b.line : b.requestedLine,
            source: args.source,
            message: b.verified ? undefined : "No E7 code at this line (yet)",
          };
        }),
      };
      this.sendResponse(response);
    } catch (e) {
      this.sendErrorResponse(response, 1004, e.message);
    }
  }

  async setExceptionBreakPointsRequest(response, args) {
    this.breakOnError = (args.filters || []).includes(ERROR_FILTER);
    if (this.client) await this.client.call("setBreakOnError", { enable: this.breakOnError }).catch(() => {});
    this.sendResponse(response);
  }

  exceptionInfoRequest(response) {
    const msg = this.lastStop?.reason === "exception" ? this.lastStop.message : "";
    response.body = { exceptionId: "RuntimeError", description: msg, breakMode: this.breakOnError ? "unhandled" : "never" };
    this.sendResponse(response);
  }

  threadsRequest(response) {
    response.body = { threads: [new Thread(THREAD_ID, "E7")] };
    this.sendResponse(response);
  }

  // ---------------------------------------------------------------- inspection
  async stackTraceRequest(response, args) {
    try {
      const frames = this.client ? await this.client.call("stack") : [];
      const start = args.startFrame || 0;
      const end = args.levels ? start + args.levels : frames.length;
      response.body = {
        stackFrames: frames.slice(start, end).map((f) => {
          const src = this.source(f.file);
          const column = utf16Column(this.lineText(src.path, f.line), f.column);
          return { id: f.id + 1, name: f.name, source: src, line: f.line, column };
        }),
        totalFrames: frames.length,
      };
      this.sendResponse(response);
    } catch (e) {
      this.sendErrorResponse(response, 1005, e.message);
    }
  }

  async scopesRequest(response, args) {
    try {
      const scopes = await this.client.call("scopes", { frameId: args.frameId - 1 });
      response.body = {
        scopes: scopes.map((s) => ({
          name: s.name,
          variablesReference: s.ref,
          namedVariables: s.count,
          expensive: s.name === "Globals",
          presentationHint: s.name === "Locals" ? "locals" : undefined,
        })),
      };
      this.sendResponse(response);
    } catch (e) {
      this.sendErrorResponse(response, 1006, e.message);
    }
  }

  toDapVariable(v) {
    const out = { name: v.name, value: v.value, type: v.type, variablesReference: v.ref };
    if (v.ref > 0) {
      if (INDEXED_TYPES.has(v.type)) out.indexedVariables = v.count;
      else out.namedVariables = v.count;
    }
    return out;
  }

  async variablesRequest(response, args) {
    try {
      let list = [];
      if (this.client && !(args.filter === "named" && args.variablesReference > 0 && this.indexedRefs?.has(args.variablesReference))) {
        list = await this.client.call("variables", { ref: args.variablesReference, start: args.start || 0, count: args.count || 0 });
      }
      response.body = { variables: list.map((v) => this.trackIndexed(this.toDapVariable(v))) };
      this.sendResponse(response);
    } catch (e) {
      this.sendErrorResponse(response, 1007, e.message);
    }
  }

  // remember which references are indexed (arrays): they have no named children
  trackIndexed(v) {
    if (!this.indexedRefs) this.indexedRefs = new Set();
    if (v.indexedVariables !== undefined) this.indexedRefs.add(v.variablesReference);
    return v;
  }

  async evaluateRequest(response, args) {
    if (!this.client || !this.lastStop) return this.sendErrorResponse(response, 1008, "not available: the script is not stopped", undefined, 0);
    try {
      const v = await this.client.call("evaluate", { frameId: args.frameId ? args.frameId - 1 : 0, expression: args.expression.trim() });
      const d = this.trackIndexed(this.toDapVariable(v));
      response.body = { result: d.value, type: d.type, variablesReference: d.variablesReference, indexedVariables: d.indexedVariables, namedVariables: d.namedVariables };
      this.sendResponse(response);
    } catch (e) {
      // hovers over things that are not variables are expected: no error popup
      this.sendErrorResponse(response, 1009, e.message, undefined, 0);
    }
  }

  // ---------------------------------------------------------------- execution
  async resume(response, action) {
    if (!this.client) return this.sendResponse(response);
    this.lastStop = null;
    this.indexedRefs = null;
    try {
      await this.client.call("resume", { action });
      if (action === "continue") response.body = { allThreadsContinued: true };
      this.sendResponse(response);
    } catch (e) {
      this.sendErrorResponse(response, 1010, e.message);
    }
  }
  continueRequest(response) {
    return this.resume(response, "continue");
  }
  nextRequest(response) {
    return this.resume(response, "next");
  }
  stepInRequest(response) {
    return this.resume(response, "stepIn");
  }
  stepOutRequest(response) {
    return this.resume(response, "stepOut");
  }
  async pauseRequest(response) {
    if (this.client) await this.client.call("pause").catch(() => {});
    this.sendResponse(response);
  }

  async terminateRequest(response) {
    await this.stop();
    this.sendResponse(response);
  }
  async disconnectRequest(response) {
    await this.stop();
    this.sendResponse(response);
  }
  async stop() {
    const c = this.client;
    if (!c) return this.end(0);
    let t = null;
    try {
      t = await Promise.race([c.call("terminate"), new Promise((r) => setTimeout(r, 500))]);
    } catch {
      t = null;
    }
    void t; // the session's remaining output and its "terminated" event arrive through onWorkerEvent
    this.end(0);
  }
}

function show(v) {
  if (typeof v === "string") return JSON.stringify(v);
  if (typeof v === "bigint") return `${v}n`;
  if (v instanceof Uint8Array) return `Uint8Array(${v.length}) ${Buffer.from(v).toString("hex")}`;
  return String(v);
}

/** Registers the `e7` debugger: configuration provider, inline adapter and the Debug E7 File command. */
function activateDebug(context, { isE7Document }) {
  const sessions = new Set();

  const provider = {
    provideDebugConfigurations() {
      return [{ type: TYPE, request: "launch", name: "Debug E7 file", program: "${file}" }];
    },
    resolveDebugConfiguration(folder, config) {
      // F5 without a launch.json: debug the active E7 file
      if (!config.type && !config.request && !config.name) {
        const ed = vscode.window.activeTextEditor;
        if (ed && isE7Document(ed.document)) {
          config.type = TYPE;
          config.name = "Debug E7 file";
          config.request = "launch";
          config.program = ed.document.uri.fsPath;
        }
      }
      if (!config.program) {
        const ed = vscode.window.activeTextEditor;
        if (ed && ed.document.uri.scheme === "file") config.program = ed.document.uri.fsPath;
      }
      if (!config.program) {
        return vscode.window.showInformationMessage("E7: open an E7 file to debug (or set \"program\" in launch.json).").then(() => undefined);
      }
      config.type = config.type || TYPE;
      config.request = config.request || "launch";
      config.name = config.name || "Debug E7 file";
      return config;
    },
    async resolveDebugConfigurationWithSubstitutedVariables(folder, config) {
      // save the program first (unsaved editors are also passed to the debugger, but this keeps line numbers obvious)
      const doc = vscode.workspace.textDocuments.find((d) => d.uri.scheme === "file" && d.uri.fsPath === config.program);
      if (doc && doc.isDirty) await doc.save();
      return config;
    },
  };

  const factory = {
    createDebugAdapterDescriptor() {
      const s = new E7DebugSession(context);
      sessions.add(s);
      s.onEnd = () => sessions.delete(s);
      return new vscode.DebugAdapterInlineImplementation(s);
    },
  };

  async function debugFile(uri) {
    let target = uri instanceof vscode.Uri ? uri : vscode.window.activeTextEditor?.document.uri;
    if (!target || target.scheme !== "file") {
      vscode.window.showWarningMessage("E7: open a saved E7 file to debug it.");
      return false;
    }
    const folder = vscode.workspace.getWorkspaceFolder(target);
    return vscode.debug.startDebugging(folder, { type: TYPE, name: `Debug ${path.basename(target.fsPath)}`, request: "launch", program: target.fsPath });
  }

  context.subscriptions.push(
    vscode.debug.registerDebugConfigurationProvider(TYPE, provider),
    vscode.debug.registerDebugAdapterDescriptorFactory(TYPE, factory),
    vscode.commands.registerCommand("e7.debugFile", debugFile),
    { dispose: () => sessions.forEach((s) => s.client?.dispose()) },
  );
}

module.exports = { activateDebug, E7DebugSession, utf16Column };
