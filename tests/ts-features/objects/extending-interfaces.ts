// @feature: Extending interfaces (single and multiple)
// @category: Object Types
// @status: supported
interface Colorful { color: string }
interface Circle { radius: number }
interface ColorfulCircle extends Colorful, Circle { label: string }
const cc: ColorfulCircle = { color: "red", radius: 42, label: "c1" };
const asCircle: Circle = cc;
console.log(cc, asCircle.radius);
