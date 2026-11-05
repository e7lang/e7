// @error: is not assignable to type 'Box<Shape>'
// instances of a generic class with different type arguments are different classes
class Shape {
  name = "s";
}
class Circle extends Shape {
  r = 1;
}
class Box<T> {
  constructor(public v: T) {}
}
const bc = new Box(new Circle());
const bs: Box<Shape> = bc;
console.log(bs.v.name);
