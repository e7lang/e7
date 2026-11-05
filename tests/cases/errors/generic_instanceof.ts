// @error: is a generic class, and e7 compiles one class per type argument list
class Box<T> {
  constructor(public v: T) {}
}
const b: Box<number> | null = new Box(1);
console.log(b instanceof Box);
