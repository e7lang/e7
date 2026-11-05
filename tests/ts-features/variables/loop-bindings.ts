// @feature: Per-iteration `let` bindings captured by closures
// @category: Variable Declaration
// @status: supported
const fns: (() => number)[] = [];
for (let i = 0; i < 3; i++) fns.push(() => i);
const gs: (() => string)[] = [];
for (const s of ["x", "y"]) gs.push(() => s);
console.log(fns.map(f => f()), gs.map(g => g()));
