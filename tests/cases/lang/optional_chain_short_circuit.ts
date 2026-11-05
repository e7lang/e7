// a?.b.c / a?.b[i] / a?.m().n(): links after '?.' are short-circuited with it, as in TypeScript
interface Inner { v: number; opt?: string }
interface Outer { inner: Inner; list: number[] }
function get(n: number): Outer | null { return n > 0 ? { inner: { v: n }, list: [n, n * 2] } : null; }
class Shape {
  r: number;
  constructor(r: number) { this.r = r; }
  area(): number { return 3 * this.r * this.r; }
}
function pick(n: number): Shape | undefined { return n > 0 ? new Shape(n) : undefined; }
console.log(get(1)?.inner.v, get(0)?.inner.v, get(2)?.list[1], get(0)?.list[1]);
console.log(pick(2)?.area().toFixed(2), pick(0)?.area().toFixed(2));
console.log(get(3)?.list.map((x: number): number => x + 1).join("-"), get(0)?.list.length);
const s: string | undefined = get(1)?.inner.opt;
console.log(s, (get(0)?.inner)?.v, get(4)?.inner.opt?.length);
