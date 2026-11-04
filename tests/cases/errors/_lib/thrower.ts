// helper module for rt_trace_import.ts
export function explode(why: string): void {
  throw new Error(why);
}
