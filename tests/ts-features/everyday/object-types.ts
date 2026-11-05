// @feature: Anonymous object types and optional properties
// @category: Everyday Types
// @status: supported
function printName(obj: { first: string; last?: string }): void {
  if (obj.last !== undefined) {
    console.log(obj.first + " " + obj.last.toUpperCase());
  } else {
    console.log(obj.first);
  }
}
printName({ first: "Bob" });
printName({ first: "Alice", last: "Alisson" });
function printCoord(pt: { x: number; y: number }): void {
  console.log("The coordinate's x value is " + pt.x + ", y is " + pt.y);
}
printCoord({ x: 3, y: 7 });
