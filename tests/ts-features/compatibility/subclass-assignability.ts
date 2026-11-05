// @feature: Subclass to superclass assignability and array covariance
// @category: Type Compatibility and Inference
// @status: supported
class Shape { name(): string { return "shape"; } }
class Circle extends Shape { name(): string { return "circle"; } }
const circles: Circle[] = [new Circle(), new Circle()];
const shapes: Shape[] = circles;
const s: Shape = new Circle();
console.log(shapes.map(x => x.name()), s.name());
