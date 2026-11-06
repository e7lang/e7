// @feature: Angle-bracket type assertions `<T>x`
// @category: Everyday Types
// @status: supported
// @note: `<T>expr` is the same as `expr as T`
const total = <number>(1 + 2);
console.log(total);
