// @error: tryParse must be used with a type assertion: tryParse(text) as MyType | null
import * as json from "e7:json";
const cfg = json.tryParse("{}");
console.log(cfg);
