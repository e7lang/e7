// built-in methods: undefined (or a value that may be undefined) for an optional argument, several or spread
// arguments of push / unshift, includes(x, from), reduce with an empty initial value, destructuring sets and
// strings, the Array constructor, Map types from entries, entries() of typed arrays
class A { constructor(public n: number) {} }
class B extends A {}
{
  function tail(s: string, end?: number): string { return s.slice(1, end); }
  function arr(a: number[], from?: number, to?: number): number[] { return a.slice(from, to); }
  function idx(a: string, x: string, from?: number): number { return a.indexOf(x, from) * 10 + a.lastIndexOf(x, from); }
  function fx(n: number, d?: number): string { return n.toFixed(d ?? 0) + n.toString(d === undefined ? undefined : 16); }
  function pad(s: string, n: number, f?: string): string { return s.padStart(n, f); }
  function jn(a: number[], sep?: string): string { return a.join(sep); }
  function fl(a: number[], v: number, st?: number): number[] { return a.fill(v, st); }
  const u = new Uint8Array([1, 2, 3, 2]);
  function ui(x: number, from?: number): number { return u.indexOf(x, from) + u.lastIndexOf(x, undefined) * 10; }
  console.log(tail("abcdef"), tail("abcdef", 3), arr([1, 2, 3, 4]), arr([1, 2, 3, 4], 1), arr([1, 2, 3, 4], undefined, 2));
  console.log(idx("abcabc", "b"), idx("abcabc", "b", 2), fx(1.25), fx(255, 2), pad("x", 3), pad("x", 3, "*"), jn([1, 2]), jn([1, 2], "-"));
  console.log(fl([0, 0, 0], 7), fl([0, 0, 0], 7, 1), ui(2), ui(2, 2), [1, 2, 1].lastIndexOf(1, undefined), "a-b-c".split("-", undefined));
}
{
  const a = [1, 2, 3, NaN];
  function inc(x: number, from?: number): boolean { return a.includes(x, from); }
  const ro: readonly string[] = ["a", "b"];
  console.log(a.includes(1, 1), a.includes(2, 1), a.includes(NaN, -1), a.includes(3, -2), inc(1), inc(1, 1), ro.includes("a", 1), [[1]].includes([1], 0));
}
{
  const a = [3];
  console.log(a.unshift(1, 2), a.unshift(...[9, 8]), a.push(...[4], 5, ...[6]), a);
  const s: string[] = ["c"];
  s.unshift("a", "b");
  const o: { n: number }[] = [];
  o.push({ n: 1 }, ...[{ n: 2 }]);
  o.unshift(...o);
  console.log(s, o, a.unshift());
}
{
  const m = new Map<string, number>([["a", 1], ["b", 2]]);
  const [[k, v], second] = m;
  const [x0, , x2] = new Uint8Array([1, 2, 3]);
  const [first, ...others] = "a😀b";
  console.log(k, v, second, x0, x2, first, others);
  function f(): void { const [p, q = 9] = new Set<number>([4]); console.log(p, q); }
  f();
}
{
  const a = new Array<string>("x");
  const b = new Array(1, 2, 3);
  const c: number[] = new Array(4);
  const d = Array<number>(2);
  const e = Array("p", "q");
  const f = new Array<boolean>(true, false);
  console.log(a, b, c.length, d.length, e, f, Array.of<number>(), Array.of(1, 2));
}
{
  const a = new Float64Array([1.5, 2.5]);
  for (const k of a.keys()) console.log(k);
  for (const v of a.values()) console.log(v);
  const b = new BigInt64Array([5n]);
  for (const [i, v] of b.entries()) console.log(i, v);
  const u = new Uint8Array([7, 8]);
  for (const [i, v] of u.entries()) console.log(i, v);
}
{
  const letters = ["a", "b", "c"];
  console.log(letters.reduce((acc: string[], x) => [x, ...acc], []), letters.reduce<Map<string, number>>((acc, x) => acc.set(x, 1), new Map()));
}
{
  const m = new Map([[1, new A(1)], [2, new B(2)]]);
  const m2 = new Map([["x", [1, 2]], ["y", []]]);
  const m3 = new Map([["k", { a: 1 }]]);
  console.log(m, m2, m3.get("k")?.a);
}
{
  function run(u: number | undefined, f: string | undefined): void {
    const s = "abcabc";
    console.log(s.slice(u), s.slice(1, u), s.substring(2, u), s.padStart(8, f), s.padEnd(8, f), s.split("b", u));
    console.log(s.indexOf("c", u), s.lastIndexOf("a", u), s.includes("a", u), s.startsWith("b", u), s.endsWith("c", u));
    console.log((123.456).toFixed(u), (123.456).toExponential(u), parseInt("ff", u), parseInt("10", u));
    console.log([1, 2, 3, 4].slice(1, u), [1, 2, 3].indexOf(3, u), [1, 2, 3].lastIndexOf(1, u), [1, 2, 3].includes(1, u));
    console.log([1, 2, 3, 4].fill(0, 2, u), [1, 2, 3, 4].splice(1, u), [1, 2, 3, 4].toSpliced(1, u));
  }
  run(undefined, undefined);
  run(1, "-");
}
