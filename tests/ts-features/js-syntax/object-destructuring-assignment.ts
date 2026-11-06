// @feature: Object destructuring assignment (`({ a, b } = obj)`)
// @category: JavaScript: Syntax
// @status: supported
// @note: as a statement of its own, with defaults, nesting, rest and property targets
let x = 0;
let y = 0;
({ x, y } = { x: 5, y: 6 });
const box = { w: 0, h: 0 };
let depth = 0;
({ w: box.w, h: box.h = 2, inner: { depth } } = { w: 3, h: undefined as number | undefined, inner: { depth: 4 } });
console.log(x, y, box, depth);
