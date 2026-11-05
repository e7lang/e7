// @feature: Type aliases
// @category: Everyday Types
// @status: supported
type Point = { x: number; y: number };
type ID = string;
type Callback = (p: Point) => number;
const norm: Callback = p => Math.hypot(p.x, p.y);
const id: ID = "p1";
console.log(id, norm({ x: 3, y: 4 }));
