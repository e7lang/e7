// @feature: Tuple types (including labeled and readonly tuples)
// @category: Object Types
// @status: supported
// @note: An index computed at run time needs a tuple whose elements all have one type (`[number, number, number]`, or literal types of one base as `as const` gives); such a tuple is also iterable and has the array methods that do not change it
type Pair = [name: string, age: number];
function describe(p: Pair): string {
  const [name, age] = p;
  return `${name} is ${age}`;
}
const t: readonly [number, string, boolean] = [1, "a", true];
const p: Pair = ["Ann", 30];
p[1] += 1;
console.log(describe(p), t, t.length, t[1]);
const v: [number, number, number] = [1, 2, 3];
let sum = 0;
for (let i = 0; i < v.length; i++) sum += v[i];
console.log(sum);
const triple: [number, number, number] = [1, 2, 3];
for (const x of triple) console.log(x);
console.log(triple.map((x) => x * 2).join(","), (["a", "b"] as const).includes("a"));
