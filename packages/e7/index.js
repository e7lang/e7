// @e7lang/e7 — the E7 compiler and VM compiled to WebAssembly, for Node.js and browsers.
// Thin wrapper over the C entry points in tools/e7wasm.cpp (structured API section).
import createE7 from "./wasm/e7wasm.js";

/** Version of this npm package. */
export const version = "1.0.1";

const STATUS = [
  "ok", "suspended", "runtime_error", "budget_exceeded", "out_of_memory", "stack_overflow", "load_error",
  "verify_error", "signature_error", "not_found", "bad_arguments", "compile_error", "invalid_state",
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
      const st = this._withString(source, (sp, sl) => this._withString(fileName, (np) => m._e7w_compile(sp, sl, np)));
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
      let sig = null;
      const list = this._json(m._e7w_exports(bp, bl));
      if (list) sig = list.find((e) => e.name === (call || "main"))?.sig ?? null;
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

// ------------------------------------------------------------------ module-level (lazy) API
let pending = null;

/**
 * Loads the WebAssembly module (once) and resolves to a synchronous E7 instance.
 * Options are passed to the Emscripten factory (e.g. wasmBinary, locateFile).
 */
export function init(options = {}) {
  if (!pending) {
    const args = {};
    if (options.wasmBinary) args.wasmBinary = options.wasmBinary;
    if (options.locateFile) args.locateFile = options.locateFile;
    if (options.wasmURL) args.locateFile = (f) => (f.endsWith(".wasm") ? String(options.wasmURL) : f);
    pending = createE7(args).then((m) => new E7(m));
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

export default { init, compile, check, run, disassemble, listExports, formatDiagnostics, version };
