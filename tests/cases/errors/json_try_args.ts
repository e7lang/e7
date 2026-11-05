// @error: tryParse takes exactly one argument
import { tryParse } from "e7:json";
const n = tryParse("1", "2") as number | null;
console.log(n);
