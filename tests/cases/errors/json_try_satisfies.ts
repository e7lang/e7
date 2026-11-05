// @error: tryParse must be used with a type assertion
import { tryParse } from "e7:json";
const n = tryParse("1") satisfies number | null;
console.log(n);
