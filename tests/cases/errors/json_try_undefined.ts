// @error: tryParse returns null when the text is not valid JSON of the expected type: write tryParse(text) as number | undefined | null
import { tryParse } from "e7:json";
const n = tryParse("1") as number | undefined;
console.log(n);
