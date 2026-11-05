function maybe(n: number): number | null {
  return n > 0 ? n : null;
}
function maybeStr(n: number): string | undefined {
  return n > 0 ? "s" + n : undefined;
}
class P {
  x: number;
  constructor(x: number) {
    this.x = x;
  }
}

let v: number | null = maybe(1);
v ??= 2;
console.log(v !== null ? v * 2 : -1);
console.log(v === null, v == null, v != null, v === undefined, v !== undefined, null === v);

const s = maybeStr(1);
if (s !== undefined) {
  if (s !== undefined) {
    console.log(s, s === undefined, s !== null, s == undefined);
  }
}

// comparisons of non-nullable values with null / undefined (allowed by tsc)
const n: number = 3;
const str: string = "a";
const b: boolean = true;
const arr: number[] = [1];
const p = new P(1);
const big: bigint = 5n;
console.log(n === null, n !== undefined, str == null, b != undefined, arr === null, p !== null, big === undefined);
console.log(undefined === n, null !== str, null == p);

let w: number | undefined = undefined;
w = 4;
console.log(w !== undefined ? w + 1 : 0, w === undefined);
const q: P | null = new P(5);
if (q !== null && q !== null) console.log(q.x);
function f(x: number | null): number {
  if (x === null) return -1;
  if (x === null) return -2;
  return x;
}
console.log(f(null), f(3));

// narrowed in a dead branch
const t = maybeStr(2);
if (t !== undefined) {
  if (t === undefined) {
    console.log("dead");
  } else {
    console.log(t.length);
  }
}
let z: number | null = maybe(-1);
z ??= 7;
z = maybe(-3);
console.log(z === null, z !== null ? z + 1 : 0);
let count = 0;
const side = (): number => ++count;
console.log(side() === null, side() !== undefined, count);
