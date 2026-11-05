import { scale, Point } from "./lib/geo.ts";

let calls = 0;
console.log("top-level start");

function add(a: number, b: number): number {
  const s = a + b;
  calls = calls + 1;
  return s;
}

function makeCounter(start: number): () => number {
  let n = start;
  return (): number => {
    n = n + 1;
    return n;
  };
}

export function main(): number {
  const p = new Point(3, 4);
  const m = new Map<string, number>();
  m.set("one", 1);
  const nested = { name: "n", inner: { list: [1, 2, 3] } };
  const counter = makeCounter(10);
  counter();
  let r = add(1, 2);
  r = add(r, p.norm());
  debugger;
  console.log("r = " + r);
  return r + scale(2);
}

export function sum(n: number, label: string): string {
  let t = 0;
  for (let i = 0; i < n; i++) {
    t = t + i;
  }
  return label + t;
}

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
