// x ??= v is a non-null value; x! as an assignment target; constant defaults for values that may be
// undefined; [null, undefined, "a"]; spreading a value that may be undefined; async arrow functions
// returning a promise; `return;` next to `return v`; Promise.reject<T>; error class types; e.message = s;
// stacked labels
class C { static inst: C | null = null; f: number[] | null = null; v = 1; }
let x: number | null = null;
const a: number = (x ??= 5);
const c = new C();
const b: number[] = (c.f ??= [1]);
const s: C = (C.inst ??= new C());
console.log(a, b, s.v);

let y: number | null = 3;
y! += 1;
const o: { a?: number } = { a: 1 };
o.a! *= 5;
console.log(y, o.a);

function g(n: bigint = 5n): bigint { return n; }
function h(n: "a" | "b" = "b"): string { return n; }
function i(n: number | null = null): string { return String(n); }
const gu: bigint | undefined = undefined;
const hu: "a" | "b" | undefined = undefined;
const nu: number | null | undefined = undefined;
console.log(g(gu), h(hu), i(nu), i(null), i(4));

const mixed = [null, undefined, "a"];
const nums = [1, null, undefined];
console.log(mixed, nums);

interface P { a: number; b?: string }
function sp(p: P | undefined, q: P | null): string {
  return JSON.stringify({ z: 1, ...p }) + JSON.stringify({ ...q, z: 2 }) + JSON.stringify({ ...q, ...p });
}
console.log(sp({ a: 1, b: "x" }, { a: 3 }), sp(undefined, null));

async function n2(v: number): Promise<number> { return v; }
const arrow = async (): Promise<number> => n2(2);
const early = (v: number) => { if (v > 2) { return; } return -v; };
console.log(early(5), early(1));
arrow().then((v) => console.log("arrow", v));
const rej = async (): Promise<number> => { try { return await Promise.reject<number>(new Error("e")); } catch { return -1; } };
rej().then((v) => console.log("rej", v));

const te: TypeError = new TypeError("q");
const re: RangeError[] = [new RangeError("r")];
const err = new Error("x");
err.message = "y";
const t: Error = te;
t.name = "Mine";
console.log(te.message, re.length, err.message, String(t));

outer: inner: for (let k = 0; k < 3; k++) {
  for (let j = 0; j < 3; j++) {
    if (j === 1) continue outer;
    if (k === 2) break inner;
    console.log(k, j);
  }
}
