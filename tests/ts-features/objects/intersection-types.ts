// @feature: Intersection types `A & B`
// @category: Object Types
// @status: unsupported
// @why: types known at compile time: intersection types are excluded (SPEC 0.1); write the combined interface (or `interface C extends A, B`)
// @instead: an interface that extends both: `interface ColorfulCircle extends Colorful, Circle {}`
// @error: intersection types are not supported
interface Colorful { color: string }
interface Circle { radius: number }
type ColorfulCircle = Colorful & Circle;
const cc: ColorfulCircle = { color: "red", radius: 42 };
console.log(cc);
