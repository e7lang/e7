export function add(a: i32, b: i32): i32 { return a + b; }
export function twice(a: u64): u64 { return a * u64(2); }
export function neg(a: i64): i64 { return -a; }
export function rev(b: Uint8Array): Uint8Array {
  const o = new Uint8Array(b.length);
  for (let i = 0; i < b.length; i++) o[i] = b[b.length - 1 - i];
  return o;
}
export function not(x: boolean): boolean { return !x; }
export function greet(name: string): string { return `héllo, ${name} 👋`; }
export function rnd(): number { return Math.random(); }
export function nan(): number { return 0 / 0; }
export function hog(): number {
  const a: string[] = [];
  for (let i = 0; i < 10000000; i++) a.push("x" + i);
  return a.length;
}
export function deep(n: number): number { return n <= 0 ? 0 : 1 + deep(n - 1); }
export function spin(): void { while (true) {} }
export function boom(): void { throw new Error("boom"); }
console.log("loaded");
