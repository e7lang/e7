// fourth test round of 0.1.3 (mixed/assign_expr_type): must print what Node.js prints
const index = new Map<string, Set<number>>();
let set = index.get("w");
if (!set) index.set("w", (set = new Set<number>()));
set.add(1);
let s: string | undefined;
const len = (s = "abc").length;
let n: number | null = null;
const doubled = (n = 21) * 2;
console.log(index, len, doubled, s, n);
