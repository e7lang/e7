// @error: 'Shape' cannot be used as a value because it was imported using 'import type'
import type { Shape } from "../modules/_lib/typeonly_types.ts";
console.log(new Shape("x"));
