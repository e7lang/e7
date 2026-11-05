// @error: unknown type 'T'
class Box<T> {
  static empty: T[] = [];
  constructor(public v: T) {}
}
console.log(new Box(1).v);
