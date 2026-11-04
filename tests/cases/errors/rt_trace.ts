// @runtime-error: rt_trace.ts:4
function inner(a: number[]): number { return a[10]; }
function outer(): number { return inner([1]); }
outer();
