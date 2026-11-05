// Compile-only check of index.d.ts (CI: tsc --noEmit --strict --module nodenext).
import e7default, { init, compile, check, run, disassemble, listExports, inspect, formatDiagnostics, version, E7Error } from "@e7lang/e7";
import type { Diagnostic, RunResult, CompileResult, E7, ExportInfo, Status, ModuleInfo } from "@e7lang/e7";
import { createDebugSession } from "@e7lang/e7";
import type { DebugSession, StoppedEvent, TerminatedEvent, BreakpointInfo, StopReason, DebugVariable } from "@e7lang/e7";

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
  const info: ModuleInfo = await inspect(bytes ?? new Uint8Array());
  const req: string | null = e7.inspect(new Uint8Array()).requiresVersion;
  const t: CompileResult = await compile("", { target: "0.2", debug: true });
  void [info.keyId, info.status, req, t];
  const v: string = version + e7.version + formatDiagnostics(d) + (await compile("")).ok;
  const e = new E7Error();
  void [line, s, out, err, text, ex, v, e.diagnostics, e7default.run];
}
async function debug(): Promise<void> {
  const s: DebugSession = await createDebugSession({ entry: "main.ts", files: { "main.ts": "" }, call: "f", args: [1], slice: 1000, budget: 0 });
  s.on("output", (line: string) => void line).on("stopped", (e: StoppedEvent) => void e.frame.line);
  s.setBreakpoints("main.ts", [3]).forEach((b: BreakpointInfo) => void b.verified);
  const ev: StoppedEvent | TerminatedEvent = await s.launch({ stopOnEntry: true });
  if (ev.event === "stopped") {
    const r: StopReason = ev.reason;
    const v: DebugVariable[] = s.variables(s.scopes(s.stack()[0].id)[0].ref, 0, 10);
    const e: DebugVariable = s.evaluate(0, "a.b");
    void [r, v, e];
  }
  s.pause();
  const t: TerminatedEvent = s.terminate();
  void t.result;
}
void main;
void debug;
