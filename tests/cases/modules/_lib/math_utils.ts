export const TAU = Math.PI * 2;
export function clamp(x: number, lo: number, hi: number): number { return Math.min(hi, Math.max(lo, x)); }
export class Accumulator {
  private total = 0;
  add(x: number): Accumulator { this.total += x; return this; }
  value(): number { return this.total; }
}
export interface Named { name: string }
export enum Level { Low = 1, High = 10 }
let calls = 0;
export function counted(): number { return ++calls; }
