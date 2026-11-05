// @feature: Merging interfaces
// @category: Declaration Merging
// @status: unsupported
// @why: not implemented yet: each type is declared once
// @instead: one interface (or an interface that extends the other)
// @error: duplicate declaration of 'Box'
interface Box { height: number; width: number }
interface Box { scale: number }
const box: Box = { height: 5, width: 6, scale: 10 };
console.log(box);
