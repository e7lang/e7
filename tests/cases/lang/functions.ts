function add(a: number, b: number): number { return a + b; }
function greet(name: string, greeting = "Hello", punct: string = "!"): string {
  return `${greeting}, ${name}${punct}`;
}
console.log(add(2, 3), greet("Ann"), greet("Bob", "Hi"), greet("Cy", "Yo", "?"));
function opt(a: number, b?: number): number { return b === undefined ? a : a + b; }
console.log(opt(1), opt(1, 2));
function sum(...xs: number[]): number { let s = 0; for (const x of xs) s += x; return s; }
console.log(sum(), sum(1), sum(1, 2, 3), sum(...[4, 5, 6]));
const sq = (x: number) => x * x;
const mul = (a: number, b: number): number => { return a * b; };
console.log(sq(9), mul(6, 7));
function makeCounter(): () => number {
  let count = 0;
  return () => ++count;
}
const c1 = makeCounter(), c2 = makeCounter();
console.log(c1(), c1(), c1(), c2());
function compose(f: (x: number) => number, g: (x: number) => number): (x: number) => number {
  return (x) => f(g(x));
}
console.log(compose(sq, (x) => x + 1)(3));
// recursion and hoisting
console.log(isEven(10), isOdd(7));
function isEven(n: number): boolean { return n === 0 ? true : isOdd(n - 1); }
function isOdd(n: number): boolean { return n === 0 ? false : isEven(n - 1); }
// closures capturing loop variables
const fns: (() => number)[] = [];
for (let i = 0; i < 3; i++) fns.push(() => i * 10);
console.log(JSON.stringify(fns.map(f => f())));
// nested functions
function outer(x: number): number {
  function inner(y: number): number { return x + y; }
  return inner(1) + inner(2);
}
console.log(outer(10));
const fact = (n: number): number => n <= 1 ? 1 : n * fact(n - 1);
console.log(fact(10), fact(20));
function apply(fs: ((n: number) => number)[], v: number): number[] { return fs.map(f => f(v)); }
console.log(apply([sq, (n) => n + 1, (n) => Math.sqrt(n)], 16));
let counter = 0;
function bump(): void { counter++; }
bump(); bump();
console.log(counter);
const memo = new Map<number, number>();
function fibm(n: number): number {
  if (n < 2) return n;
  const c = memo.get(n);
  if (c !== undefined) return c;
  const r = fibm(n - 1) + fibm(n - 2);
  memo.set(n, r);
  return r;
}
console.log(fibm(80));
