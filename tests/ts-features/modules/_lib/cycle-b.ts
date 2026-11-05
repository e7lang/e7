// helper module: imports cycle-a, which imports this module
import { a } from "./cycle-a.ts";
export const b = 2;
export function twiceA(): number { return 2 * a; }
