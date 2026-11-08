// assignment evaluates the target's object and index before the value (JavaScript order)
const log: string[] = [];
function t<T>(tag: string, v: T): T { log.push(tag); return v; }
class F { x = 0; }
let o = new F();
t("L", o).x = t("R", 1);
console.log(log.join(","));
const fa = [new F(), new F()];
let i = 0;
fa[i++].x = fa[i].x = 7;
console.log(fa[0].x, fa[1].x, i);
const first = o;
o.x = (o = new F(), 5);
console.log(first.x, o.x);
const a = [0, 0, 0];
let j = 0;
a[j++] = j;
console.log(a.join(","));
// logical assignment evaluates the target once
const b: (number | undefined)[] = [undefined, 7];
let k = 0;
b[k++] ??= 10;
console.log(k, b.join(","));
let calls = 0;
const holder = { n: 0 };
function get(): { n: number } { calls++; return holder; }
get().n ||= 5;
get().n &&= 6;
console.log(calls, holder.n);
// postfix ++ on an accessor whose value is used evaluates the receiver once
class Acc { private v = 1; get n(): number { return this.v; } set n(x: number) { this.v = x; } }
const accs = [new Acc(), new Acc()];
let m = 0;
const old = accs[m++].n++;
console.log(old, accs[0].n, accs[1].n, m);
