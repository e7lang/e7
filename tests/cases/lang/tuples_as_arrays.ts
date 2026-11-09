// a tuple whose elements have one type (or literal types of one base) is iterable, has the array
// methods that do not change it, and is read with computed indexes; spreading `as const` tuples
const COLORS = ["red", "green"] as const;
for (const c of COLORS) console.log(c);
const pair: [number, number] = [1, 2];
for (const x of pair) console.log(x);
console.log([...COLORS], COLORS.includes("red"), COLORS.indexOf("green"));
const v: [number, number, number] = [1, 2, 3];
console.log(v.map((x) => x * 2), v.join("-"), v.reduce((a, b) => a + b, 0), v.some((x) => x > 2), v.slice(1));
const DIRS = ["n", "s", "e"] as const;
const NUMS = [1, 2, 3] as const;
let i = 1;
const d: string = DIRS[i];
const n: number = NUMS[i];
console.log(d, n, DIRS[i % 3], NUMS[2 - i]);
const consts = [1, 2, 3] as const;
const copy = [...consts];
for (const c of [...consts]) console.log(c);
console.log(copy, consts.length, consts[1]);
const a = "x";
const b = "y";
const strs = [a, b];
strs.push("z");
console.log(strs);
