// @feature: Object destructuring assignment (`({ a, b } = obj)`)
// @category: JavaScript: Syntax
// @status: unsupported
// @why: not implemented yet: only array patterns can be assigned to existing variables (docs/SPEC.md section 3 does not make the difference)
// @instead: a declaration (`const { a, b } = obj`) or one assignment per property
// @error: invalid assignment target
let x = 0;
let y = 0;
({ x, y } = { x: 5, y: 6 });
console.log(x, y);
