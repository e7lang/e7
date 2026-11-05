// @runtime-error: at JSON.parse (native)
// @runtime-error: at load (rt_trace_native.ts:4:40)
interface Cfg { a: number }
function load(s: string): Cfg { return JSON.parse(s) as Cfg; }
console.log(load("{oops").a);
