// @error: tryParse needs a type to parse into: tryParse(text) as MyType | null
import { tryParse } from "e7:json";
const n = tryParse("1") as null;
console.log(n);
