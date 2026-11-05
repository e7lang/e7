// @feature: Function compatibility with fewer parameters
// @category: Type Compatibility and Inference
// @status: partial
// @why: not implemented yet: a function *value* with fewer parameters is not converted to a function type with more
// @instead: a wrapper arrow: `const y: (b: number, s: string) => number = (b) => x(b)`
// @error: type '(p0: number) => number' is not assignable to type '(p0: number, p1: string) => number'
const x = (a: number) => 0;
const y: (b: number, s: string) => number = x;
console.log(y(1, "a"));
