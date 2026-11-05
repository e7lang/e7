// functions that refer to themselves: a local const arrow (typed, or annotated), a named function
// expression (its name is in scope in its body only), fresh bindings per loop iteration; defaults that
// refer to earlier parameters in declarations, methods, constructors and static methods
function locals(): number {
  const fact = (n: number): number => (n <= 1 ? 1 : n * fact(n - 1));
  const fib: (n: number) => number = (n) => (n < 2 ? n : fib(n - 1) + fib(n - 2));
  let sum = (xs: number[], i: number): number => (i >= xs.length ? 0 : xs[i] + sum(xs, i + 1));
  const fs: (() => number)[] = [];
  for (let i = 0; i < 3; i++) {
    const down = (m: number): number => (m <= 0 ? i : down(m - 1));
    fs.push(() => down(2));
  }
  return fact(5) + fib(10) + sum([1, 2, 3], 0) + fs.map((f) => f()).reduce((a, b) => a + b, 0);
}
const fact = function f(n: number): number { return n <= 1 ? 1 : n * f(n - 1); };
const squares = [3, 4].map(function sq(x: number): number { return x <= 0 ? 0 : x * x; });
function inLoop(): number[] {
  const out: number[] = [];
  for (let i = 0; i < 3; i++) {
    const g = function rec(n: number): number { return n <= 0 ? i : rec(n - 1) + 1; };
    out.push(g(2));
  }
  return out;
}
console.log(locals(), fact(6), squares, inLoop(), fact);

function f(a: number, b = a + 1, c: string = "x".repeat(b)): string { return a + ":" + b + ":" + c; }
class Box {
  readonly w: number;
  constructor(w: number, h = w * 2) { this.w = w + h; }
  area(scale: number, by = scale * this.w): number { return by; }
  static of(n: number, m = n + 1): Box { return new Box(n, m); }
}
console.log(f(1), f(1, 3), f(2, 1, "y"), new Box(1).w, new Box(1, 1).w, new Box(2).area(2), Box.of(3).w);
