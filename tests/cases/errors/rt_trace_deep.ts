// @runtime-error: deep failure
// @runtime-error: at f29 (rt_trace_deep.ts:
// @runtime-error: ... 6 more lines
// @runtime-error: at <init> (rt_trace_deep.ts:
// A deep stack of 30 different functions (no repetition to fold): the trace keeps the 20 innermost and
// the 5 outermost lines and says how many it left out.
function f29(n: number): number { if (n > 0) abort("deep failure"); return n; }
function f28(n: number): number { return f29(n + 1) + 1; }
function f27(n: number): number { return f28(n + 1) + 1; }
function f26(n: number): number { return f27(n + 1) + 1; }
function f25(n: number): number { return f26(n + 1) + 1; }
function f24(n: number): number { return f25(n + 1) + 1; }
function f23(n: number): number { return f24(n + 1) + 1; }
function f22(n: number): number { return f23(n + 1) + 1; }
function f21(n: number): number { return f22(n + 1) + 1; }
function f20(n: number): number { return f21(n + 1) + 1; }
function f19(n: number): number { return f20(n + 1) + 1; }
function f18(n: number): number { return f19(n + 1) + 1; }
function f17(n: number): number { return f18(n + 1) + 1; }
function f16(n: number): number { return f17(n + 1) + 1; }
function f15(n: number): number { return f16(n + 1) + 1; }
function f14(n: number): number { return f15(n + 1) + 1; }
function f13(n: number): number { return f14(n + 1) + 1; }
function f12(n: number): number { return f13(n + 1) + 1; }
function f11(n: number): number { return f12(n + 1) + 1; }
function f10(n: number): number { return f11(n + 1) + 1; }
function f9(n: number): number { return f10(n + 1) + 1; }
function f8(n: number): number { return f9(n + 1) + 1; }
function f7(n: number): number { return f8(n + 1) + 1; }
function f6(n: number): number { return f7(n + 1) + 1; }
function f5(n: number): number { return f6(n + 1) + 1; }
function f4(n: number): number { return f5(n + 1) + 1; }
function f3(n: number): number { return f4(n + 1) + 1; }
function f2(n: number): number { return f3(n + 1) + 1; }
function f1(n: number): number { return f2(n + 1) + 1; }
function f0(n: number): number { return f1(n + 1) + 1; }
console.log(f0(0));
