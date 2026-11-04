// allocation-heavy workload to exercise the garbage collector
class Node3 { constructor(public v: number, public next: Node3 | null) {} }
let keep: Node3 | null = null;
let total = 0;
for (let round = 0; round < 200; round++) {
  let list: Node3 | null = null;
  for (let i = 0; i < 1000; i++) list = new Node3(i, list);
  let s = 0;
  for (let n = list; n !== null; n = n.next) s += n.v;
  total += s;
  if (round % 50 === 0) keep = list;
  const strs: string[] = [];
  for (let i = 0; i < 100; i++) strs.push("item" + i + ":" + round);
  total += strs.join("").length;
  const m = new Map<string, number[]>();
  for (let i = 0; i < 50; i++) m.set("k" + (i % 10), [i, i + 1]);
  total += m.size;
}
let kept = 0;
for (let n = keep; n !== null; n = n.next) kept++;
console.log(total, kept);
const closures: (() => number)[] = [];
for (let i = 0; i < 10000; i++) { const v = i * 2; closures.push(() => v); }
let cs = 0;
for (const f of closures) cs += f();
console.log(cs);
