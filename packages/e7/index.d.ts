// Type definitions for @e7lang/e7

/** Version of this npm package. */
export declare const version: string;
export declare const DEFAULT_BUDGET: number;
export declare const DEFAULT_MEMORY_LIMIT: number;

export interface Diagnostic {
  /** File name as given by `fileName` (or the import path for imported files). */
  file: string;
  /** 1-based line. */
  line: number;
  /** 1-based column in UTF-16 code units (JavaScript string indices + 1). */
  column: number;
  message: string;
  severity: "error" | "warning";
}

export interface CompileOptions {
  /** Name of the entry file, used in diagnostics and to resolve relative imports. Default "main.ts". */
  fileName?: string;
  /** Other source files for relative imports, keyed by path relative to the entry's directory. */
  files?: Record<string, string>;
  /** Synchronous reader used to load relative imports on demand (e.g. fs.readFileSync in Node). */
  readFile?: (path: string) => string | undefined;
  /** Add debug information (statement boundaries, local variable names, columns) to the bytecode. */
  debug?: boolean;
  /**
   * Oldest e7vm that must run the module, e.g. "0.2" (or a number major << 16 | minor << 8 | patch).
   * Features newer than the target are compile errors. Default: this VM's version. Throws E7Error if malformed.
   */
  target?: string | number;
}

export interface CompileResult {
  ok: boolean;
  /** Portable E7 bytecode (.e7b) when ok. */
  bytecode?: Uint8Array;
  diagnostics: Diagnostic[];
}

/**
 * Name of an e7_status (`code` holds the number). "pending" (15) is a call waiting for async host
 * functions; this package registers none, so async exports and top-level await finish within run()
 * (e7:test's later() is settled by the VM itself).
 */
export type Status =
  | "ok" | "suspended" | "runtime_error" | "budget_exceeded" | "out_of_memory" | "stack_overflow" | "load_error"
  | "verify_error" | "signature_error" | "not_found" | "bad_arguments" | "compile_error" | "invalid_state"
  | "paused" | "version_error" | "pending";

/** Values accepted as arguments of an exported function. */
export type E7Argument = number | bigint | boolean | string | Uint8Array | ArrayBuffer | null;
/** Values returned by an exported function (i64/u64 as bigint, void as undefined). */
export type E7Result = number | bigint | boolean | string | Uint8Array | null | undefined;

export interface RunOptions extends CompileOptions {
  /** Instruction budget; 0 or Infinity = unlimited. Default 100,000,000. */
  budget?: number;
  /** VM heap limit in bytes. Default 64 MiB. */
  memoryLimit?: number;
  /** Seed for Math.random / e7:crypto randomness (deterministic runs). */
  seed?: number;
  /** Exported function to call after the top-level statements. Default: main() if it exists. */
  call?: string;
  /** Arguments for `call`. */
  args?: E7Argument[];
}

export interface RunResult {
  ok: boolean;
  status: Status;
  /** Numeric e7_status (see include/e7/e7.h). */
  code: number;
  /** One entry per console.log call. */
  output: string[];
  /** Return value of the called function (undefined for void or on error). */
  result: E7Result;
  /** Error text (compile, runtime, budget, ...) or null on success. */
  error: string | null;
  /** Compile diagnostics when status is "compile_error". */
  diagnostics?: Diagnostic[];
}

export interface ExportInfo {
  name: string;
  params: string[];
  returns: string;
}

/** Header of a .e7b module (e7_inspect). */
export interface ModuleInfo {
  /** true if this VM can load the module's format and version (the signature is not checked). */
  ok: boolean;
  /** "ok", "version_error" (format or required e7vm too new) or "load_error" (not a module / corrupted). */
  status: Status;
  code: number;
  /** Bytecode format version (2 for e7vm 0.2). */
  format: number;
  /** Oldest e7vm that can run the module, e.g. "0.2.0" (null for format 1). */
  requiresVersion: string | null;
  /** The same as major << 16 | minor << 8 | patch. */
  requiresVersionNumber: number;
  /** An Ed25519 signature trailer is present (not verified here). */
  signed: boolean;
  /** First 8 bytes of SHA-256(public key) of the signing key, hex; null when unsigned. */
  keyId: string | null;
  debugInfo: boolean;
}

export interface InitOptions {
  /** The contents of e7wasm.wasm (skips fetching / reading the file). */
  wasmBinary?: ArrayBuffer | Uint8Array;
  /** URL of e7wasm.wasm (default: next to the module, via import.meta.url). */
  wasmURL?: string | URL;
  /** Emscripten locateFile hook. */
  locateFile?: (file: string, prefix: string) => string;
}

export declare class E7Error extends Error {
  diagnostics: Diagnostic[];
}

/** Synchronous API, available after `await init()`. */
export interface E7 {
  /** Version of the E7 VM inside the WebAssembly module. */
  readonly version: string;
  compile(source: string, options?: CompileOptions): CompileResult;
  check(source: string, options?: CompileOptions): Diagnostic[];
  run(input: string | Uint8Array, options?: RunOptions): RunResult;
  disassemble(input: string | Uint8Array, options?: CompileOptions): string;
  listExports(input: string | Uint8Array, options?: CompileOptions): ExportInfo[];
  inspect(bytecode: Uint8Array): ModuleInfo;
}

// ------------------------------------------------------------------ debugger

export declare const DEFAULT_DEBUG_SLICE: number;

export interface DebugSessionOptions extends InitOptions {
  /** Entry file name (relative imports resolve from its directory). Default "main.ts". */
  entry?: string;
  /** Source of the entry file. Default: `files[entry]`, then `readFile(entry)`. */
  source?: string;
  /** Source files keyed by path relative to the entry's directory (the entry may be one of them). */
  files?: Record<string, string>;
  /** Synchronous reader used to load the entry and relative imports on demand. */
  readFile?: (path: string) => string | undefined;
  /** Exported function to call after the top-level statements. Default: main() if it exists. */
  call?: string;
  /** Arguments for `call` (with `args` and no `call`, main is called). */
  args?: E7Argument[];
  /** VM heap limit in bytes. Default 64 MiB. */
  memoryLimit?: number;
  seed?: number;
  /** Instructions per time slice; the session yields to the event loop between slices. Default 200,000. */
  slice?: number;
  /** Total instruction budget, counted in whole slices; 0 or Infinity = unlimited (default). */
  budget?: number;
  /** Stop on runtime errors that are about to terminate the script. Default true. */
  breakOnError?: boolean;
  /** Also stop on errors that a try/catch handler will take (default false). */
  breakOnCaught?: boolean;
}

export type StopReason = "entry" | "breakpoint" | "step" | "debugger" | "pause" | "exception";

export interface DebugLocation {
  /** Function name (`<init>` for top-level code, `Class.method` for methods). */
  name: string;
  /** File relative to the entry file's directory (`main.ts`, `lib/util.ts`, `../shared/x.ts`). */
  file: string;
  line: number;
  /** 1-based column (UTF-8 bytes). */
  column: number;
}

export interface StackFrame extends DebugLocation {
  /** Frame id: 0 = innermost. */
  id: number;
}

export interface StoppedEvent {
  event: "stopped";
  reason: StopReason;
  /** Error message for reason "exception". */
  message: string;
  /** Ids of the breakpoints hit. */
  breakpoints: number[];
  frame: DebugLocation;
  /** Lines printed since the previous event (also delivered through on("output")). */
  output: string[];
}

export interface TerminatedEvent {
  event: "terminated";
  ok: boolean;
  /** "aborted" when the session was terminated by terminate(). */
  status: Status | "aborted";
  code: number;
  result: E7Result;
  error: string | null;
  output: string[];
}

export interface BreakpointInfo {
  id: number;
  /** Line of the statement the breakpoint stops at (first statement on or after the requested line). */
  line: number;
  /** Resolved to code (breakpoints set before launch() are verified when the module loads). */
  verified: boolean;
  requestedLine?: number;
  file?: string;
}

export interface DebugScope {
  /** "Locals", "Closure" or "Globals". */
  name: string;
  ref: number;
  count: number;
}

export interface DebugVariable {
  name: string;
  /** number, boolean, i32, ..., string, null, undefined, Array, Map, Set, Object, tuple, Uint8Array, bigint, function, Date or a class name. */
  type: string;
  /** Display value (console.log format, nested values abbreviated). */
  value: string;
  /** > 0 when the value has children: pass it to variables(). */
  ref: number;
  /** Number of children. */
  count: number;
}

export type DebugSessionState = "idle" | "running" | "stopped" | "terminated";

export interface DebugSession {
  readonly state: DebugSessionState;
  readonly entry: string;
  on(event: "output", listener: (line: string) => void): this;
  on(event: "stopped", listener: (e: StoppedEvent) => void): this;
  on(event: "continued", listener: () => void): this;
  on(event: "terminated", listener: (e: TerminatedEvent) => void): this;
  off(event: string, listener: (...args: any[]) => void): this;
  /** Replaces the breakpoints of a file (relative to the entry's directory, or absolute: matched by path suffix). */
  setBreakpoints(file: string, lines: number[]): BreakpointInfo[];
  breakpoints(): BreakpointInfo[];
  setBreakOnError(enable: boolean): void;
  /** Also stop on errors that a try/catch handler will take (before the handler runs). */
  setBreakOnCaught(enable: boolean): void;
  /** Runs top-level code, then `call`, until the script stops or terminates. */
  launch(options?: { stopOnEntry?: boolean }): Promise<StoppedEvent | TerminatedEvent>;
  continue(): Promise<StoppedEvent | TerminatedEvent>;
  next(): Promise<StoppedEvent | TerminatedEvent>;
  stepIn(): Promise<StoppedEvent | TerminatedEvent>;
  stepOut(): Promise<StoppedEvent | TerminatedEvent>;
  /** Stops a running script at its next statement (the pending launch/continue/... resolves with reason "pause"). */
  pause(): void;
  /** Inspection (while stopped; references are invalidated when the script resumes). */
  stack(): StackFrame[];
  scopes(frameId?: number): DebugScope[];
  variables(ref: number, start?: number, count?: number): DebugVariable[];
  /** A variable followed by `.field`, `[i]`, `['key']`, `.length`, `.size`. Throws E7Error. */
  evaluate(frameId: number, expression: string): DebugVariable;
  terminate(): TerminatedEvent;
}

/** Compiles a program with debug information and opens a debug session on a new WebAssembly instance. */
export declare function createDebugSession(options: DebugSessionOptions): Promise<DebugSession>;

export declare function init(options?: InitOptions): Promise<E7>;
export declare function compile(source: string, options?: CompileOptions): Promise<CompileResult>;
export declare function check(source: string, options?: CompileOptions): Promise<Diagnostic[]>;
export declare function run(input: string | Uint8Array, options?: RunOptions): Promise<RunResult>;
export declare function disassemble(input: string | Uint8Array, options?: CompileOptions): Promise<string>;
export declare function listExports(input: string | Uint8Array, options?: CompileOptions): Promise<ExportInfo[]>;
export declare function inspect(bytecode: Uint8Array): Promise<ModuleInfo>;
export declare function formatDiagnostics(diagnostics: Diagnostic[]): string;
export declare function normalizePath(path: string): string;

declare const _default: {
  init: typeof init;
  compile: typeof compile;
  check: typeof check;
  run: typeof run;
  disassemble: typeof disassemble;
  listExports: typeof listExports;
  inspect: typeof inspect;
  formatDiagnostics: typeof formatDiagnostics;
  createDebugSession: typeof createDebugSession;
  version: string;
};
export default _default;
