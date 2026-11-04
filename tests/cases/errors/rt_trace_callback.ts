// @runtime-error: at cmp (rt_trace_callback.ts:3)
// @runtime-error: (native)
function cmp(a: number, b: number): number { if (a === 2) abort("bad element"); return a - b; }
console.log([3, 2, 1].sort(cmp).join());
