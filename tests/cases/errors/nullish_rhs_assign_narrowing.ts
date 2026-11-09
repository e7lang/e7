// @error: object is possibly 'null'
// the right side of ?? runs only when the left is null or undefined: y may still be null
let x: string | undefined = "a";
let y: string | null = null;
const v = x ?? (y = "v");
console.log(v, y.length);
