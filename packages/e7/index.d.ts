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
}

export interface CompileResult {
  ok: boolean;
  /** Portable E7 bytecode (.e7b) when ok. */
  bytecode?: Uint8Array;
  diagnostics: Diagnostic[];
}

export type Status =
  | "ok" | "runtime_error" | "budget_exceeded" | "out_of_memory" | "stack_overflow" | "load_error"
  | "verify_error" | "signature_error" | "not_found" | "bad_arguments" | "compile_error" | "invalid_state";

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
}

export declare function init(options?: InitOptions): Promise<E7>;
export declare function compile(source: string, options?: CompileOptions): Promise<CompileResult>;
export declare function check(source: string, options?: CompileOptions): Promise<Diagnostic[]>;
export declare function run(input: string | Uint8Array, options?: RunOptions): Promise<RunResult>;
export declare function disassemble(input: string | Uint8Array, options?: CompileOptions): Promise<string>;
export declare function listExports(input: string | Uint8Array, options?: CompileOptions): Promise<ExportInfo[]>;
export declare function formatDiagnostics(diagnostics: Diagnostic[]): string;
export declare function normalizePath(path: string): string;

declare const _default: {
  init: typeof init;
  compile: typeof compile;
  check: typeof check;
  run: typeof run;
  disassemble: typeof disassemble;
  listExports: typeof listExports;
  formatDiagnostics: typeof formatDiagnostics;
  version: string;
};
export default _default;
