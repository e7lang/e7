// A tuple whose elements all have one type can be indexed with a computed index.
function parse(v: string): [number, number, number] {
  const p = v.split(".");
  return [Number(p[0]), Number(p[1]), Number(p[2])];
}
function cmp(a: string, b: string): number {
  const x = parse(a), y = parse(b);
  for (let i = 0; i < 3; i++) if (x[i] !== y[i]) return x[i] < y[i] ? -1 : 1;
  return 0;
}
const names: readonly [string, string] = ["a", "b"];
let s = "";
for (let i = 0; i < names.length; i++) s += names[i];
console.log(cmp("1.2.3", "1.10.0"), cmp("2.0.0", "1.9.9"), cmp("1.0.0", "1.0.0"), s);
