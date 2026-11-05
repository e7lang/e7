// @feature: Tuple types (including labeled and readonly tuples)
// @category: Object Types
// @status: supported
type Pair = [name: string, age: number];
function describe(p: Pair): string {
  const [name, age] = p;
  return `${name} is ${age}`;
}
const t: readonly [number, string, boolean] = [1, "a", true];
const p: Pair = ["Ann", 30];
p[1] += 1;
console.log(describe(p), t, t.length, t[1]);
