// helper module: imports cycle-b, which imports this module
import { b } from "./cycle-b.ts";
export const a = 1;
export function sumAB(): number { return a + b; }
