// @feature: Ergonomic brand checks (`#x in obj`)
// @category: Classes
// @status: unsupported
// @why: types known at compile time: private fields always exist on instances of their class
// @instead: `instanceof`
// @error: '#x in obj' is not supported
class Point {
  #x = 0;
  static isPoint(o: Point | null): boolean { return o !== null && #x in o; }
}
console.log(Point.isPoint(new Point()));
