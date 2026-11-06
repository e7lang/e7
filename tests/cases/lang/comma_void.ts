// the comma operator (also in for updates) and the void operator
let log: string[] = [];
function f(s: string): number { log.push(s); return s.length; }
let x = (f("a"), f("bb"), f("ccc"));
for (let i = 0, j = 10; i < j; i++, j--) log.push(`${i}-${j}`);
const u: number | undefined = void f("v");
const v: undefined = void 0;
let k = 0;
k = (k++, k++, k + 10);
console.log(x, u, v, k, log.join(","));
