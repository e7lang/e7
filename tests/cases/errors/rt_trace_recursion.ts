// @runtime-error: index 1 out of range
// @runtime-error: at down (rt_trace_recursion.ts:7:50)
// @runtime-error: at down (rt_trace_recursion.ts:8:10)
// @runtime-error: ... 99 more frames of down (recursion)
// @runtime-error: at <init> (rt_trace_recursion.ts:10:13)
function down(n: number): number {
  if (n === 0) { const a: number[] = []; return a[1]; }
  return down(n - 1) + 1;
}
console.log(down(100));
