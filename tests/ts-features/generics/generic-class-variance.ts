// @feature: Structural assignability between instantiations (`Box<Circle>` to `Box<Shape>`)
// @category: Generics
// @status: unsupported
// @why: types known at compile time: each instantiation is a separate class with its own field layout
// @instead: construct the instance with the wider type argument: `new Box<Shape>(circle)`
// @error: type 'Box<Circle>' is not assignable to type 'Box<Shape>'
class Shape { name = "shape"; }
class Circle extends Shape { r = 1; }
class Box<T> { constructor(public v: T) {} }
const c = new Box(new Circle());
const s: Box<Shape> = c;
console.log(s.v.name);
