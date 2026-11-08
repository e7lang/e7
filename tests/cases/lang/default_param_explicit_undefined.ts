// an explicit undefined takes the default also when the parameter's type includes undefined
function f(n: number | undefined = 3): number | undefined { return n; }
class K { m(n: number | undefined = 5): number | undefined { return n; } }
class A { constructor(public n: number | undefined = 7, public o: { a: number } | undefined = { a: 1 }) {} }
function h(o: { a: number } | undefined = { a: 2 }, s: string | null | undefined = "d"): string { return `${o?.a} ${s}`; }
const a = new A(undefined, undefined);
console.log(f(undefined), f(), f(1), new K().m(undefined), a.n, a.o?.a);
console.log(h(undefined, undefined), h(undefined, null), h({ a: 9 }));
