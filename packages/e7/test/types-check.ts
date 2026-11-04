// Compile-only check of index.d.ts (CI: tsc --noEmit --strict --module nodenext).
import e7default, { init, compile, check, run, disassemble, listExports, formatDiagnostics, version, E7Error } from "@e7lang/e7";
import type { Diagnostic, RunResult, CompileResult, E7, ExportInfo, Status } from "@e7lang/e7";

async function main(): Promise<void> {
  const e7: E7 = await init({ wasmURL: new URL("https://example.com/e7wasm.wasm") });
  const c: CompileResult = e7.compile("let x = 1;", { fileName: "a.ts", files: { "./b.ts": "" } });
  const bytes: Uint8Array | undefined = c.bytecode;
  const d: Diagnostic[] = await check("x", { readFile: (p: string) => (p ? undefined : "") });
  const line: number = d[0].line + d[0].column;
  const r: RunResult = await run(bytes ?? "src", { budget: 1e6, memoryLimit: 1 << 20, seed: 1, call: "f", args: [1, 2n, "s", true, null, new Uint8Array()] });
  const s: Status = r.status;
  const out: string[] = r.output;
  const err: string | null = r.error;
  const text: string = await disassemble("src");
  const ex: ExportInfo[] = await listExports(new Uint8Array());
  const v: string = version + e7.version + formatDiagnostics(d) + (await compile("")).ok;
  const e = new E7Error();
  void [line, s, out, err, text, ex, v, e.diagnostics, e7default.run];
}
void main;
