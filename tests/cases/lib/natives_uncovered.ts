// Natives no other test called (0.2.5 coverage report, scripts/coverage.py), compared with Node.js.
// Math results are printed rounded: the last bit of a libm result may differ between platforms.
const r = (x: number): string => x.toFixed(9);
console.log(r(Math.sin(0.5)), r(Math.cos(0.5)), r(Math.tan(0.5)), r(Math.asin(0.5)), r(Math.acos(0.5)), r(Math.atan(0.5)));
console.log(r(Math.atan2(1, -2)), r(Math.sinh(0.5)), r(Math.cosh(0.5)), r(Math.tanh(0.5)), r(Math.asinh(0.5)),
  r(Math.acosh(1.5)), r(Math.atanh(0.5)));
console.log(r(Math.exp(1.5)), r(Math.expm1(1e-5)), r(Math.log(10)), r(Math.log10(1234)), r(Math.log1p(1e-5)),
  r(Math.log2(1000)), r(Math.cbrt(-27.5)));
console.log(Math.ceil(-1.5), Math.ceil(1.2), Math.trunc(-1.7), Math.trunc(2.9), Math.sign(-3), Math.sign(0), Math.sign(4));
console.log(Math.pow(2, 10), Math.pow(2, -1), Math.pow(-8, 1 / 3), Math.fround(5.5), Math.fround(0.1), Math.clz32(1), Math.clz32(0));
console.log(Math.acos(2), Math.log(-1), Math.sqrt(-1), Math.atanh(1), Math.log(0));
const xs = [3, -1, 7.5, 2];
console.log(Math.max(...xs), Math.min(...xs), Math.max(...([] as number[])), Math.min(...([] as number[])));

console.log(JSON.stringify({ a: 1, b: [1, 2], c: { d: "x" } }, null, "--"));
console.log(JSON.stringify([[1], [2, 3]], null, ""));
console.log(decodeURI("%E4%BD%A0%E5%A5%BD%20%3Fx%3D1%26y"), decodeURI("a%2Fb"));
let caught = "";
try {
  decodeURI("%E4%BD");
} catch (e) {
  caught = e instanceof URIError ? "URIError" : "other";
}
console.log(caught);

// string and number arrays: search, fill, unshift; pop()! / shift()! (no boxing)
const s = ["a", "b", "c", "b"];
console.log(s.includes("b"), s.includes("z"), s.indexOf("b"), s.indexOf("b", 2), s.lastIndexOf("b"), s.lastIndexOf("a", -2));
console.log(s.unshift("z"), s.join(","), s.fill("q", 1, 3).join(","), s.fill("w").join(","));
const st = ["x", "y", "z"];
const last: string = st.pop()!;
const first: string = st.shift()!;
console.log(last, first, st.length);
const ns = [1.5, 2.5, 3.5];
const nl: number = ns.pop()!;
const nf: number = ns.shift()!;
console.log(nl, nf, ns.length);

// Map get()! / get() ?? default for every key / value kind
const msn = new Map<string, number>([["a", 1]]);
const mns = new Map<number, string>([[1, "one"]]);
const mss = new Map<string, string>([["k", "v"]]);
const mnn = new Map<number, number>([[2, 4]]);
const mnb = new Map<number, boolean>([[3, true]]);
const msb = new Map<string, boolean>([["t", false]]);
console.log(msn.get("a")!, mns.get(1)!, mss.get("k")!, mnn.get(2)!, mnb.get(3)!, msb.get("t")!);
console.log(msn.get("z") ?? -1, mns.get(9) ?? "none", mss.get("q") ?? "dflt", mnn.get(5) ?? 0, mnb.get(9) ?? false,
  msb.get("u") ?? true);

const bytes = new Uint8Array([1, 2, 3]);
const copy = Uint8Array.from(bytes);
copy[0] = 9;
const copy2 = new Uint8Array(bytes);
copy2[1] = 8;
console.log(bytes.join(","), copy.join(","), copy2.join(","));
const buf = new ArrayBuffer(8);
const view = new Uint8Array(buf);
for (let i = 0; i < 8; i++) view[i] = i * 10;
const part = buf.slice(2, 5);
const part2 = buf.slice(-2);
console.log(part.byteLength, new Uint8Array(part).join(","), part2.byteLength, new Uint8Array(buf.slice(1)).length);
console.log(new Date().getTime() > 1600000000000);
console.error("console.error goes to stderr (not compared)");

// String() of values without a dedicated conversion (core.cpp valueToString)
const tup: [number, string][] = [[1, "a"], [2, "b"]];
console.log(String(new Uint8Array([1, 2, 300])), String(new Map<string, number>()), String(new Set<number>()), [1n, 22n].join("-"));
console.log(String(tup), `${[new Uint8Array([5])]}`, String({ a: 1 }), [{ b: 2 }].join(), String([new Map<string, number>()]));
