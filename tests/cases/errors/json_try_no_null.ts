// @error: tryParse returns null when the text is not valid JSON of the expected type: write tryParse(text) as Config | null
import { tryParse } from "e7:json";
interface Config { port: number }
const cfg = tryParse("{}") as Config;
console.log(cfg.port);
