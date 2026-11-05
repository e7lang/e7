// @error: tryParse must be used with a type assertion: tryParse(text) as MyType | null
import { tryParse } from "e7:json";
const cfg = tryParse("{}");
console.log(cfg);
