import { clamp } from "./math_utils.ts";
export function pad(n: number, width: number): string { return String(clamp(n, 0, 999)).padStart(width, "0"); }
export function shout(s: string): string { return s.toUpperCase() + "!"; }
