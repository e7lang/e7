// @feature: Interfaces
// @category: Everyday Types
// @status: supported
interface Point {
  x: number;
  y: number;
}
function printCoord(pt: Point): void {
  console.log(`(${pt.x}, ${pt.y})`);
}
const p: Point = { x: 100, y: 100 };
printCoord(p);
printCoord({ x: 1, y: 2 });
