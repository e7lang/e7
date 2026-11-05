import { scale, Counter } from "./lib/util.ts";
class Point {
  constructor(public x: number, public y: number) {}
  norm(): number {
    const sq = this.x * this.x + this.y * this.y;
    return Math.sqrt(sq);
  }
}
let total = 0;
const names: string[] = ["a", "b"];
function add(a: number, b: number): number {
  const s = a + b;
  total = total + s;
  return s;
}
function makeCounter(start: i32): () => i32 {
  let n: i32 = start;
  return (): i32 => {
    n = n + 1;
    return n;
  };
}
export function main(): number {
  const p = new Point(3, 4);
  const flag = true;
  const big = 12345678901234567890n;
  const m = new Map<string, number>();
  m.set("k1", 1);
  const st = new Set<number>([7, 8]);
  const arr = [[1, 2], [3]];
  const rec = { name: "r", inner: { v: 1 } };
  const tup: [number, string] = [1, "t"];
  const bytes = new Uint8Array(3);
  const nothing: string | null = null;
  const c = makeCounter(10);
  c();
  let r = add(1, 2);
  r = add(r, p.norm());
  debugger;
  r = r + scale(2) + new Counter().count;
  return r;
}
add(5, 6);
export function fail(): number {
  const xs: number[] = [1];
  return xs[5];
}
export function spin(): number {
  let i = 0;
  while (i >= 0) {
    i = i + 1;
  }
  return i;
}
export function log(): void {
  console.log("hello from prog");
}
