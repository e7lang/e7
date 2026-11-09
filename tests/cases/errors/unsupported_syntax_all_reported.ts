// @error: 15:15: error: index signatures are not supported
// @error: 17:10: error: 'keyof' types are not supported
// @error: 18:14: error: indexed access types
// @error: 19:25: error: intersection types are not supported
// @error: 20:15: error: conditional types
// @error: 21:13: error: index signatures are not supported
// @error: 22:1: error: 'var' is not supported
// @error: 25:19: error: for...in is not supported
// @error: 26:11: error: the 'in' operator is not supported
// @error: 26:17: error: 'delete' is not supported
// @error: 27:14: error: regular expressions are not supported
// @error: 31:21: error: type 'string' is not assignable to type 'number'
// Unsupported syntax that parses is reported and parsed past: one compile reports all of it, and the checker
// still reports the other errors (and none caused by the unsupported parts).
type Dict = { [k: string]: number };
interface Opts { [key: string]: string; name: string }
type K = keyof Opts;
type V = Opts["name"];
type AB = { a: number } & { b: string };
type C<T> = T extends string ? 1 : 2;
class Bag { [k: string]: number; size = 0; }
var legacy = 1;
function f(d: Dict, o: Opts): number {
  let t = 0;
  for (const k in d) t += d[k];
  if ("x" in d) delete d.x;
  const re = /ab+c/g;
  console.log(re, o.name);
  return t + legacy;
}
const bad: number = "str";
console.log(f({}, { name: "n" }));
