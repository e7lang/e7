// @feature: Function overloads
// @category: More on Functions
// @status: unsupported
// @why: types known at compile time: the implementation signature needs a union type
// @instead: separate functions with distinct names (`makeDateFromMs`, `makeDateFromParts`)
// @error: function overloads / declarations without body are not supported
function len(s: string): number;
function len(arr: number[]): number;
function len(x: string | number[]): number {
  return x.length;
}
console.log(len("abc"), len([1, 2]));
