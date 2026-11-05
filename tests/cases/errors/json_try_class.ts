// @error: type 'Box' cannot be produced by JSON.parse
import { tryParse } from "e7:json";
class Box { v = 0; get(): number { return this.v; } }
const b = tryParse("{}") as Box | null;
console.log(b);
