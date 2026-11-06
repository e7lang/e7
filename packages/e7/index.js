// @e7lang/e7 — the E7 compiler and VM compiled to WebAssembly, for Node.js and browsers.
// Thin wrapper over the C entry points in tools/e7wasm.cpp (structured API section).
import createE7 from "./wasm/e7wasm.js";

/** Version of this npm package. */
export const version = "0.2.7";

const STATUS = [
  "ok", "suspended", "runtime_error", "budget_exceeded", "out_of_memory", "stack_overflow", "load_error",
  "verify_error", "signature_error", "not_found", "bad_arguments", "compile_error", "invalid_state", "paused",
  "version_error",
];

export const DEFAULT_BUDGET = 100_000_000;
export const DEFAULT_MEMORY_LIMIT = 64 * 1024 * 1024;

/** Error thrown by disassemble() (and by run()/compile() for invalid options). */
export class E7Error extends Error {
  constructor(message, diagnostics = []) {
    super(message);
    this.name = "E7Error";
    this.diagnostics = diagnostics;
  }
}

const enc = new TextEncoder();
const dec = new TextDecoder();

// ------------------------------------------------------------------ paths (mirror src/compiler/compiler.cpp)
export function normalizePath(p) {
  const abs = p.startsWith("/");
  const parts = [];
  for (const cur of p.split(/[/\\]/)) {
    if (cur === "..") {
      if (parts.length && parts[parts.length - 1] !== "..") parts.pop();
      else if (!abs) parts.push("..");
    } else if (cur && cur !== ".") parts.push(cur);
  }
  const out = (abs ? "/" : "") + parts.join("/");
  return out || ".";
}
function dirOf(p) {
  const i = Math.max(p.lastIndexOf("/"), p.lastIndexOf("\\"));
  return i < 0 ? "" : p.slice(0, i);
}
const IMPORT_RE = /\b(?:import|export)\s*(?:[^"'`;]*?\bfrom\s*)?["']([^"'\n]+)["']/g;

// Collects the relative imports of `source` (transitively) through a synchronous readFile callback.
function collectFiles(entry, source, readFile, files) {
  const seen = new Set([entry]);
  const queue = [[entry, source]];
  while (queue.length) {
    const [from, text] = queue.pop();
    for (const m of text.matchAll(IMPORT_RE)) {
      const spec = m[1];
      if (!(spec.startsWith(".") || spec.startsWith("/"))) continue;
      const dir = dirOf(from);
      const base = normalizePath(spec.startsWith("/") ? spec : (dir ? dir + "/" : "") + spec);
      const cands = ["", ".ts", ".e7", "/index.ts", "/index.e7"].map((e) => base + e);
      if (base.endsWith(".js")) cands.push(base.slice(0, -3) + ".ts", base.slice(0, -3) + ".e7");
      for (const c of cands) {
        if (seen.has(c) || files.has(c)) break;
        let content;
        try {
          content = readFile(c);
        } catch {
          content = undefined;
        }
        if (typeof content === "string") {
          seen.add(c);
          files.set(c, content);
          queue.push([c, content]);
          break;
        }
      }
    }
  }
}

// Compiler column (1-based) -> UTF-16 column (1-based). The E7 lexer advances its column by one per
// UTF-8 byte in code and comments but by one per code point inside string and template literals, so
// the line is re-scanned with the same rules (tracking literals that span lines).
function utf16Column(text, line, col) {
  if (typeof text !== "string" || line < 1 || col <= 1) return Math.max(1, col);
  let ln = 1;
  let mode = "code"; // code | str | tmpl | line | block
  let quote = "";
  const tmpl = []; // brace depth at each open `${`
  let depth = 0;
  let lexCol = 1;
  let i = 0;
  const n = text.length;
  while (i < n) {
    const cp = text.codePointAt(i);
    const units = cp > 0xffff ? 2 : 1;
    const ch = text[i];
    if (ln === line && lexCol >= col) break;
    if (ch === "\n") {
      if (ln === line) break;
      ln++;
      lexCol = 1;
      if (mode === "line" || mode === "str") mode = "code";
      i++;
      continue;
    }
    const literal = mode === "str" || mode === "tmpl";
    const width = literal || cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    const next = text[i + 1];
    let consumed = units;
    let adv = width;
    if (mode === "code") {
      if (ch === "/" && next === "/") mode = "line";
      else if (ch === "/" && next === "*") { mode = "block"; consumed = 2; adv = 2; }
      else if (ch === '"' || ch === "'") { mode = "str"; quote = ch; }
      else if (ch === "`") mode = "tmpl";
      else if (ch === "{") depth++;
      else if (ch === "}") {
        if (tmpl.length && tmpl[tmpl.length - 1] === depth) { tmpl.pop(); mode = "tmpl"; }
        else depth--;
      }
    } else if (mode === "block") {
      if (ch === "*" && next === "/") { mode = "code"; consumed = 2; adv = 2; }
    } else if (mode === "str") {
      if (ch === "\\") { consumed = 1 + (next !== undefined && next !== "\n" ? (text.codePointAt(i + 1) > 0xffff ? 2 : 1) : 0); adv = consumed > 1 ? 2 : 1; }
      else if (ch === quote) mode = "code";
    } else if (mode === "tmpl") {
      if (ch === "\\") { consumed = 1 + (next !== undefined && next !== "\n" ? (text.codePointAt(i + 1) > 0xffff ? 2 : 1) : 0); adv = consumed > 1 ? 2 : 1; }
      else if (ch === "`") mode = "code";
      else if (ch === "$" && next === "{") { tmpl.push(depth); mode = "code"; consumed = 2; adv = 2; }
    }
    if (ln === line && lexCol + adv > col) break; // column points inside this token piece
    lexCol += adv;
    i += consumed;
  }
  // UTF-16 index of position i relative to the start of the line
  const lineStart = text.lastIndexOf("\n", i - 1) + 1;
  return i - lineStart + 1 + Math.max(0, col - lexCol);
}

function toBytes(x) {
  if (x instanceof Uint8Array) return x;
  if (ArrayBuffer.isView(x)) return new Uint8Array(x.buffer, x.byteOffset, x.byteLength);
  if (x instanceof ArrayBuffer) return new Uint8Array(x);
  return null;
}

function hexToBytes(h) {
  const out = new Uint8Array(h.length >> 1);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(h.substr(i * 2, 2), 16);
  return out;
}

// "0.2" | "0.2.7" | "v0.2" | 0x000200 -> version text for e7w_compile_target (validated in C)
function targetString(t) {
  if (typeof t === "number") {
    if (!Number.isInteger(t) || t < 0 || t > 0xffffff) throw new E7Error(`invalid target version ${t}`);
    return versionString(t);
  }
  if (typeof t !== "string") throw new TypeError('compile(): target must be a version string such as "0.2"');
  return t;
}

function versionString(v) {
  return `${(v >> 16) & 0xff}.${(v >> 8) & 0xff}.${v & 0xff}`;
}

function parseNumber(v) {
  if (typeof v === "number") return v;
  return v === "-0" ? -0 : Number(v); // "NaN", "Infinity", "-Infinity"
}

// ------------------------------------------------------------------ instance
class E7 {
  constructor(m) {
    this._m = m;
    /** Version of the E7 VM inside the WebAssembly module. */
    this.version = this._str(m._e7w_version());
  }

  _str(p) {
    const h = this._m.HEAPU8;
    let e = p;
    while (h[e] !== 0) e++;
    return dec.decode(h.subarray(p, e));
  }
  _json(p) {
    return JSON.parse(this._str(p));
  }
  _withBytes(bytes, fn) {
    const m = this._m;
    const p = m._malloc(bytes.length + 1);
    if (!p) throw new E7Error("out of WebAssembly memory");
    try {
      m.HEAPU8.set(bytes, p);
      m.HEAPU8[p + bytes.length] = 0;
      return fn(p, bytes.length);
    } finally {
      m._free(p);
    }
  }
  _withString(s, fn) {
    return this._withBytes(enc.encode(String(s)), fn);
  }

  /** Compiles one E7 source file (plus `files` / `readFile` for relative imports). */
  compile(source, options = {}) {
    if (typeof source !== "string") throw new TypeError("compile(): source must be a string");
    const fileName = options.fileName || "main.ts";
    const files = new Map();
    for (const [k, v] of Object.entries(options.files || {})) files.set(normalizePath(k), String(v));
    if (typeof options.readFile === "function") collectFiles(fileName, source, options.readFile, files);
    const m = this._m;
    m._e7w_files_clear();
    try {
      for (const [k, v] of files) this._withString(k, (kp) => this._withString(v, (vp, vl) => m._e7w_file_add(kp, vp, vl)));
      const flags = options.debug ? 1 : 0;
      const target = options.target == null ? "" : targetString(options.target);
      const st = this._withString(source, (sp, sl) =>
        this._withString(fileName, (np) => this._withString(target, (tp) => m._e7w_compile_target(sp, sl, np, flags, tp))));
      if (st === 10) throw new E7Error(this._json(m._e7w_diagnostics())[0]?.message ?? "invalid compile options");
      if (st === 0) {
        const ptr = m._e7w_bytes_ptr();
        const len = m._e7w_bytes_len();
        return { ok: true, bytecode: m.HEAPU8.slice(ptr, ptr + len), diagnostics: [] };
      }
      const texts = new Map(files);
      texts.set(fileName, source);
      const diagnostics = this._json(m._e7w_diagnostics()).map((d) => ({
        file: d.file,
        line: Math.max(1, d.line),
        column: utf16Column(texts.get(d.file), d.line, d.col),
        message: d.message,
        severity: "error",
      }));
      return { ok: false, diagnostics };
    } finally {
      m._e7w_files_clear();
    }
  }

  /** Type checks a source file; returns its diagnostics ([] when it compiles). */
  check(source, options = {}) {
    return this.compile(source, options).diagnostics;
  }

  /**
   * Reads a module's header without loading it (e7_inspect): bytecode format, the oldest e7vm that
   * can run it, signature trailer, debug information, and whether this VM can load it.
   */
  inspect(bytecode) {
    const b = toBytes(bytecode);
    if (!b) throw new TypeError("inspect(): expected bytecode (Uint8Array)");
    const i = this._withBytes(b, (p, n) => this._json(this._m._e7w_inspect(p, n)));
    return {
      ok: i.status === 0,
      status: STATUS[i.status] ?? `status_${i.status}`,
      code: i.status,
      format: i.format,
      requiresVersion: i.format >= 2 ? versionString(i.requires) : null,
      requiresVersionNumber: i.requires,
      signed: i.signed,
      keyId: i.signed ? i.keyId : null,
      debugInfo: i.debug,
    };
  }

  /** Lists the exported functions of a module with their host signatures. */
  listExports(input, options = {}) {
    const bytes = this._bytecode(input, options);
    const list = this._withBytes(bytes, (p, n) => this._json(this._m._e7w_exports(p, n)));
    if (!list) throw new E7Error(this._str(this._m._e7w_error()));
    return list.map(({ name, sig }) => {
      const [params, ret] = sig.split(">");
      return { name, params: [...params].map(typeName), returns: typeName(ret) };
    });
  }

  _bytecode(input, options) {
    const b = toBytes(input);
    if (b) return b;
    if (typeof input !== "string") throw new TypeError("expected E7 source (string) or bytecode (Uint8Array)");
    const r = this.compile(input, options);
    if (!r.ok) throw new E7Error(formatDiagnostics(r.diagnostics), r.diagnostics);
    return r.bytecode;
  }

  /** Bytecode listing of a source file or a compiled module. Throws E7Error on compile errors. */
  disassemble(input, options = {}) {
    const bytes = this._bytecode(input, options);
    const text = this._withBytes(bytes, (p, n) => this._str(this._m._e7w_disasm_bytes(p, n)));
    if (!text) throw new E7Error(this._str(this._m._e7w_error()) || "invalid bytecode");
    return text;
  }

  /** Compiles (if needed) and runs a module: top-level statements, then `call` (default: main). */
  run(input, options = {}) {
    let bytes = toBytes(input);
    if (!bytes) {
      if (typeof input !== "string") throw new TypeError("run(): expected E7 source (string) or bytecode (Uint8Array)");
      const r = this.compile(input, options);
      if (!r.ok) {
        return {
          ok: false, status: "compile_error", code: 11, output: [], result: undefined,
          error: formatDiagnostics(r.diagnostics), diagnostics: r.diagnostics,
        };
      }
      bytes = r.bytecode;
    }
    const m = this._m;
    const call = options.call ?? "";
    const args = options.args ?? [];
    if (!Array.isArray(args)) throw new TypeError("run(): args must be an array");
    if (args.length && !call) throw new TypeError("run(): args require `call`");
    let budget = options.budget ?? DEFAULT_BUDGET;
    if (!(budget > 0) || !Number.isFinite(budget)) budget = 0;
    let mem = options.memoryLimit ?? DEFAULT_MEMORY_LIMIT;
    if (!(mem > 0) || !Number.isFinite(mem)) mem = 0;
    const seed = options.seed == null ? -1 : Number(options.seed);

    return this._withBytes(bytes, (bp, bl) => {
      // host signature of the called function: decides integer / bigint conversions
      const sig = this._signature(bp, bl, call || "main");
      const params = sig ? sig.split(">")[0] : "";
      m._e7w_args_clear();
      args.forEach((a, i) => this._pushArg(a, params[i]));
      const st = this._withString(call, (cp) => m._e7w_exec(bp, bl, cp, budget, mem, seed));
      m._e7w_args_clear();
      const out = {
        ok: st === 0,
        status: STATUS[st] ?? `status_${st}`,
        code: st,
        output: this._json(m._e7w_output_json()),
        result: st === 0 ? fromValue(this._json(m._e7w_result_json()), sig ? sig.split(">")[1] : "") : undefined,
        error: st === 0 ? null : this._str(m._e7w_error()),
      };
      return out;
    });
  }

  // host signature ("nn>n") of an exported function of a module, or null
  _signature(bp, bl, name) {
    const list = this._json(this._m._e7w_exports(bp, bl));
    return list ? list.find((e) => e.name === name)?.sig ?? null : null;
  }

  _pushArg(a, code) {
    const m = this._m;
    const isInt = code >= "1" && code <= "8";
    if (isInt && (typeof a === "number" || typeof a === "bigint")) {
      if (typeof a === "number" && !Number.isInteger(a)) throw new TypeError(`argument ${a} is not an integer`);
      const s = BigInt(a).toString();
      this._withString(s, (p) => m._e7w_arg_int(p, code === "2" || code === "4" || code === "6" || code === "8" ? 1 : 0));
      return;
    }
    if (typeof a === "number") m._e7w_arg_number(a);
    else if (typeof a === "boolean") m._e7w_arg_bool(a ? 1 : 0);
    else if (typeof a === "string") this._withString(a, (p, n) => m._e7w_arg_string(p, n));
    else if (typeof a === "bigint") this._withString(a.toString(), (p) => m._e7w_arg_int(p, a < 0n ? 0 : 1));
    else if (a === null || a === undefined) m._e7w_arg_null();
    else {
      const b = toBytes(a);
      if (!b) throw new TypeError(`unsupported argument type: ${Object.prototype.toString.call(a)}`);
      this._withBytes(b, (p, n) => m._e7w_arg_bytes(p, n));
    }
  }
}

function typeName(c) {
  return {
    n: "number", b: "boolean", s: "string", y: "Uint8Array", h: "KeyHandle", v: "void",
    1: "i8", 2: "u8", 3: "i16", 4: "u16", 5: "i32", 6: "u32", 7: "i64", 8: "u64",
  }[c] ?? "unknown";
}

function fromValue(v, retCode) {
  switch (v.type) {
    case "number": return parseNumber(v.value);
    case "boolean": return v.value;
    case "string": return v.value;
    case "int":
    case "uint":
      return retCode === "7" || retCode === "8" ? BigInt(v.value) : Number(v.value);
    case "bytes": return hexToBytes(v.value);
    case "null": return null;
    case "handle": return BigInt(v.value);
    default: return undefined;
  }
}

/** Formats diagnostics as "file:line:column: error: message" lines. */
export function formatDiagnostics(diagnostics) {
  return diagnostics.map((d) => `${d.file}:${d.line}:${d.column}: ${d.severity}: ${d.message}`).join("\n");
}

// ------------------------------------------------------------------ debugger
// A debug session drives the JSON debugger protocol of tools/e7wasm.cpp (docs/DEBUGGING.md §7) on its
// own WebAssembly instance. Script code runs in time slices: between two slices the session yields to
// the event loop, so pause() / terminate() / setBreakpoints() calls get in while a script runs.

export const DEFAULT_DEBUG_SLICE = 200_000;

const yieldToEventLoop = () =>
  new Promise((r) => (typeof setImmediate === "function" ? setImmediate(r) : setTimeout(r, 0)));

class DebugSession {
  constructor(e7, bytecode, options) {
    this._e7 = e7;
    this._m = e7._m;
    this._bytecode = bytecode;
    this._call = options.call ?? "";
    this._args = options.args ?? [];
    this._slice = options.slice > 0 && Number.isFinite(options.slice) ? Math.floor(options.slice) : DEFAULT_DEBUG_SLICE;
    this._budget = options.budget > 0 && Number.isFinite(options.budget) ? options.budget : 0;
    this._listeners = new Map();
    this._state = "idle";
    this._used = 0;
    this._busy = null;
    this._final = null;
    this._autoEntry = false;
    this.entry = options.entry;
    const mem = options.memoryLimit ?? DEFAULT_MEMORY_LIMIT;
    const seed = options.seed == null ? -1 : Number(options.seed);
    this._m._e7w_dbg_create(mem > 0 && Number.isFinite(mem) ? mem : 0, seed, this._slice);
    if (options.breakOnError === false) this._m._e7w_dbg_break_on_error(0);
    if (options.breakOnCaught === true) this._m._e7w_dbg_break_on_caught(1);
  }

  /** "idle" (not launched), "running", "stopped" or "terminated". */
  get state() {
    return this._state;
  }

  on(event, listener) {
    if (!this._listeners.has(event)) this._listeners.set(event, new Set());
    this._listeners.get(event).add(listener);
    return this;
  }
  off(event, listener) {
    this._listeners.get(event)?.delete(listener);
    return this;
  }
  _emit(event, value) {
    for (const l of this._listeners.get(event) ?? []) {
      try {
        l(value);
      } catch (e) {
        console.error(e);
      }
    }
  }

  _live() {
    if (this._state === "terminated") throw new E7Error("the debug session has terminated");
    return this._m;
  }
  _dbg(fn, ...strings) {
    const m = this._live();
    const go = (i, ptrs) =>
      i === strings.length ? this._e7._json(m[fn](...ptrs)) : this._e7._withString(strings[i], (p) => go(i + 1, [...ptrs, p]));
    return go(0, []);
  }

  /** Replaces the breakpoints of `file` (a path relative to the entry file's directory, or an absolute path). */
  setBreakpoints(file, lines) {
    if (this._state === "terminated") return lines.map((l) => ({ id: 0, requestedLine: l, line: l, verified: false }));
    const list = (lines ?? []).map((l) => Math.floor(Number(l))).filter((l) => l > 0);
    return this._dbg("_e7w_dbg_set_breakpoints", String(file), list.join(","));
  }

  /** All breakpoints with their current state (breakpoints set before launch() are verified once the code is loaded). */
  breakpoints() {
    return this._state === "terminated" ? [] : this._dbg("_e7w_dbg_breakpoints");
  }

  /** Stop on runtime errors that are about to terminate the script (default: on). */
  setBreakOnCaught(enable) {
    if (this._state !== "terminated") this._m._e7w_dbg_break_on_caught(enable ? 1 : 0);
  }

  setBreakOnError(enable) {
    if (this._state !== "terminated") this._m._e7w_dbg_break_on_error(enable ? 1 : 0);
  }

  /** Loads the module and runs it until it stops or terminates. */
  launch(options = {}) {
    if (this._state !== "idle") return Promise.reject(new E7Error("launch(): the session was already launched"));
    const e7 = this._e7;
    const m = this._m;
    let call = this._call;
    const args = this._args;
    if (!Array.isArray(args)) return Promise.reject(new TypeError("args must be an array"));
    if (args.length && !call) call = "main";
    // The script always starts with a stop at its first statement, so that top-level code runs in time
    // slices too (pausable); unless stopOnEntry was asked for, that stop is skipped.
    this._autoEntry = !options.stopOnEntry;
    this._retCode = "";
    return this._run(() =>
      e7._withBytes(this._bytecode, (bp, bl) => {
        const sig = e7._signature(bp, bl, call || "main");
        this._retCode = sig ? sig.split(">")[1] : "";
        const params = sig ? sig.split(">")[0] : "";
        m._e7w_args_clear();
        try {
          args.forEach((a, i) => e7._pushArg(a, params[i]));
          return e7._withString(call, (cp) => e7._json(m._e7w_dbg_launch(bp, bl, cp, 1)));
        } finally {
          m._e7w_args_clear();
        }
      }),
    );
  }

  continue() {
    return this._resume("continue");
  }
  next() {
    return this._resume("next");
  }
  stepIn() {
    return this._resume("stepIn");
  }
  stepOut() {
    return this._resume("stepOut");
  }

  _resume(action) {
    if (this._state === "terminated") return Promise.resolve(this._final);
    if (this._state !== "stopped") return Promise.reject(new E7Error(`${action}(): the script is not stopped`));
    return this._run(() => this._dbg("_e7w_dbg_resume", action));
  }

  /** Requests a stop at the next statement (takes effect between two time slices). */
  pause() {
    if (this._state === "running") this._m._e7w_dbg_pause();
  }

  async _run(first) {
    this._state = "running";
    this._emit("continued");
    let p;
    this._busy = p = this._loop(first);
    try {
      return await p;
    } finally {
      if (this._busy === p) this._busy = null;
    }
  }

  async _loop(first) {
    let ev;
    try {
      ev = first();
    } catch (e) {
      return this._finish({ event: "terminated", status: 12, result: { type: "void" }, error: String(e?.message ?? e), output: [] });
    }
    for (;;) {
      if (this._state === "terminated") return this._final;
      for (const line of ev.output ?? []) this._emit("output", line);
      if (ev.event === "running") {
        this._used += this._slice;
        if (this._budget && this._used >= this._budget) {
          const t = this._dbg("_e7w_dbg_terminate");
          return this._finish({ ...t, status: 3, error: "instruction budget exhausted" });
        }
        await yieldToEventLoop();
        if (this._state === "terminated") return this._final;
        ev = this._dbg("_e7w_dbg_resume", "continue");
      } else if (ev.event === "stopped") {
        if (this._autoEntry && ev.reason === "entry") {
          // the module is loaded (breakpoints are resolved): let the caller see that before the script runs
          this._autoEntry = false;
          await yieldToEventLoop();
          if (this._state === "terminated") return this._final;
          ev = this._dbg("_e7w_dbg_resume", "continue");
          continue;
        }
        this._autoEntry = false;
        this._state = "stopped";
        const stop = { event: "stopped", reason: ev.reason, message: ev.message, breakpoints: ev.breakpoints, frame: ev.frame, output: ev.output ?? [] };
        this._emit("stopped", stop);
        return stop;
      } else {
        return this._finish(ev, true);
      }
    }
  }

  _finish(ev, outputEmitted = false) {
    if (this._state === "terminated") return this._final;
    if (!outputEmitted) for (const line of ev.output ?? []) this._emit("output", line);
    const code = ev.status ?? 0;
    this._final = {
      event: "terminated",
      ok: code === 0,
      status: code === -1 ? "aborted" : STATUS[code] ?? `status_${code}`,
      code,
      result: code === 0 && ev.result ? fromValue(ev.result, this._retCode ?? "") : undefined,
      error: code === 0 ? null : ev.error || null,
      output: ev.output ?? [],
    };
    this._state = "terminated";
    this._m = null;
    this._emit("terminated", this._final);
    return this._final;
  }

  /** Call stack, innermost frame first (id 0). Empty unless stopped. */
  stack() {
    return this._state === "stopped" ? this._dbg("_e7w_dbg_stack") : [];
  }
  /** Scopes of a frame: Locals, Closure, Globals ({ name, ref, count }). */
  scopes(frameId = 0) {
    return this._state === "stopped" ? this._json(this._m._e7w_dbg_scopes(Number(frameId))) : [];
  }
  /** Children of a scope or of an expandable value (ref > 0). count 0 = all. */
  variables(ref, start = 0, count = 0) {
    if (this._state !== "stopped") return [];
    return this._json(this._m._e7w_dbg_variables(Number(ref), Number(start) || 0, Number(count) || 0));
  }
  /** Evaluates a variable path (`a.b[0]['k'].length`) in a frame. Throws E7Error when it cannot. */
  evaluate(frameId, expression) {
    if (this._state !== "stopped") throw new E7Error("evaluate(): the script is not stopped");
    const m = this._m;
    const r = this._e7._withString(String(expression), (p) => this._e7._json(m._e7w_dbg_evaluate(Number(frameId) || 0, p)));
    if (!r.ok) throw new E7Error(r.error || "cannot evaluate");
    const { ok: _ok, ...v } = r;
    return v;
  }
  _json(p) {
    return this._e7._json(p);
  }

  /** Ends the session; a script that is still running or stopped is aborted. */
  terminate() {
    if (this._state === "terminated") return this._final;
    const idle = this._state === "idle";
    const ev = this._dbg("_e7w_dbg_terminate");
    return this._finish(idle ? ev : { ...ev, status: -1, error: "terminated by the debugger" });
  }
}

function initArgs(options) {
  const args = {};
  if (options.wasmBinary) args.wasmBinary = options.wasmBinary;
  if (options.locateFile) args.locateFile = options.locateFile;
  if (options.wasmURL) args.locateFile = (f) => (f.endsWith(".wasm") ? String(options.wasmURL) : f);
  return args;
}

/**
 * Compiles a program with debug information and creates a debug session for it on a new WebAssembly
 * instance. Throws E7Error (with diagnostics) on compile errors.
 */
export async function createDebugSession(options = {}) {
  const entry = options.entry || options.fileName || "main.ts";
  let source = options.source;
  if (source == null && options.files) source = options.files[entry];
  if (source == null && typeof options.readFile === "function") source = options.readFile(entry);
  if (typeof source !== "string") throw new TypeError(`createDebugSession(): no source for ${entry}`);
  const files = { ...(options.files || {}) };
  delete files[entry];
  const m = await createE7(initArgs(options));
  const e7 = new E7(m);
  const r = e7.compile(source, { fileName: entry, files, readFile: options.readFile, debug: true });
  if (!r.ok) throw new E7Error(formatDiagnostics(r.diagnostics), r.diagnostics);
  return new DebugSession(e7, r.bytecode, { ...options, entry });
}

// ------------------------------------------------------------------ module-level (lazy) API
let pending = null;

/**
 * Loads the WebAssembly module (once) and resolves to a synchronous E7 instance.
 * Options are passed to the Emscripten factory (e.g. wasmBinary, locateFile).
 */
export function init(options = {}) {
  if (!pending) {
    pending = createE7(initArgs(options)).then((m) => new E7(m));
    pending.catch(() => {
      pending = null;
    });
  }
  return pending;
}

// A trap inside WebAssembly (e.g. a native stack overflow) leaves the instance unusable: drop it so
// that the next call starts a fresh one.
async function call(name, args) {
  const e7 = await init();
  try {
    return e7[name](...args);
  } catch (e) {
    if (typeof WebAssembly !== "undefined" && e instanceof WebAssembly.RuntimeError) pending = null;
    throw e;
  }
}

/** Async (lazy-initializing) versions of the instance methods. */
export const compile = (source, options) => call("compile", [source, options]);
export const check = (source, options) => call("check", [source, options]);
export const run = (input, options) => call("run", [input, options]);
export const disassemble = (input, options) => call("disassemble", [input, options]);
export const listExports = (input, options) => call("listExports", [input, options]);
export const inspect = (bytecode) => call("inspect", [bytecode]);

export default { init, compile, check, run, disassemble, listExports, inspect, formatDiagnostics, createDebugSession, version };
