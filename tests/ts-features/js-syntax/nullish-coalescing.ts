// @feature: Nullish coalescing `??` and `??=`
// @category: JavaScript: Syntax
// @status: supported
let timeout: number | null = null;
const t = timeout ?? 1000;
const zero: number | undefined = 0;
timeout ??= 500;
timeout ??= 700;
console.log(t, zero ?? 42, zero || 42, timeout);
