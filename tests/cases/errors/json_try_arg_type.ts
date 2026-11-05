// @error: type 'number' is not assignable to type 'string'
import { tryParse } from "e7:json";
const n = tryParse(42) as number | null;
console.log(n);
