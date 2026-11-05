// @feature: Angle-bracket type assertions `<T>x`
// @category: Everyday Types
// @status: unsupported
// @why: not implemented yet: only the `as` form is parsed
// @instead: `x as T`
// @error: expected identifier
const total = <number>(1 + 2);
console.log(total);
