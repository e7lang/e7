// @feature: Intersection types `A & B`
// @category: Object Types
// @status: unsupported
// @why: not implemented yet: intersections are not resolved into an object type
// @instead: an interface that extends both: `interface ColorfulCircle extends Colorful, Circle {}`
// @error: intersection types are not supported
interface Colorful { color: string }
interface Circle { radius: number }
type ColorfulCircle = Colorful & Circle;
const cc: ColorfulCircle = { color: "red", radius: 42 };
console.log(cc);
