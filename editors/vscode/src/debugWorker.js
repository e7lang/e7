// E7 debugger worker (node:worker_threads). Runs one @e7lang/e7 debug session on its own WebAssembly
// instance, so that the extension host never blocks while a script runs (even in a long native call:
// the extension can always kill the worker).
//
// Messages from the extension:  { id, method, params }  ->  { id, result } | { id, error: { message, diagnostics } }
// Events to the extension:      { event: "output" | "stopped" | "terminated" | "breakpoints", body }
const { parentPort, workerData } = require("worker_threads");
const fs = require("fs");
const path = require("path");
const { createDebugSession } = require("@e7lang/e7");

let session = null;
const post = (event, body) => parentPort.postMessage({ event, body });

function readFileIn(dir, overlay) {
  return (p) => {
    const abs = path.resolve(dir, p);
    if (Object.prototype.hasOwnProperty.call(overlay, abs)) return overlay[abs];
    try {
      return fs.statSync(abs).isFile() ? fs.readFileSync(abs, "utf8") : undefined;
    } catch {
      return undefined;
    }
  };
}

// a pending launch / resume: its result arrives as a "stopped" or "terminated" event
function run(promise) {
  promise.catch((e) => post("terminated", { event: "terminated", ok: false, status: "invalid_state", code: 12, error: String(e?.message ?? e), output: [] }));
}

const methods = {
  async create({ program, overlay = {}, call, args, memoryLimit, budget, slice, breakOnError, breakOnCaught }) {
    const dir = path.dirname(program);
    session = await createDebugSession({
      entry: path.basename(program),
      readFile: readFileIn(dir, overlay),
      call: call || "",
      args: args || [],
      memoryLimit,
      budget,
      slice,
      breakOnError,
      breakOnCaught,
      wasmBinary: fs.readFileSync(workerData.wasmPath),
    });
    session.on("output", (line) => post("output", line));
    session.on("stopped", (e) => post("stopped", e));
    session.on("terminated", (e) => post("terminated", e));
    return { entry: session.entry };
  },
  setBreakpoints: ({ file, lines }) => session.setBreakpoints(file, lines),
  breakpoints: () => session.breakpoints(),
  setBreakOnError: ({ enable }) => session.setBreakOnError(enable),
  setBreakOnCaught: ({ enable }) => session.setBreakOnCaught(enable),
  launch({ stopOnEntry }) {
    run(session.launch({ stopOnEntry }));
    // the module is loaded synchronously by launch(): breakpoints are resolved now
    return session.breakpoints();
  },
  resume({ action }) {
    run(session[action]());
  },
  pause: () => session.pause(),
  stack: () => session.stack(),
  scopes: ({ frameId }) => session.scopes(frameId),
  variables: ({ ref, start, count }) => session.variables(ref, start, count),
  evaluate: ({ frameId, expression }) => session.evaluate(frameId, expression),
  terminate: () => (session ? session.terminate() : null),
};

parentPort.on("message", async ({ id, method, params }) => {
  try {
    if (method !== "create" && !session) throw new Error("no debug session");
    const result = await methods[method](params || {});
    parentPort.postMessage({ id, result });
  } catch (e) {
    parentPort.postMessage({ id, error: { message: String(e?.message ?? e), diagnostics: e?.diagnostics } });
  }
});
