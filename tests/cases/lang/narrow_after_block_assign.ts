// if (!x) { x = new X(); ...more statements } narrows x after the if.
const m = new Map<string, Set<number>>();
function add(k: string, v: number): void {
  let set = m.get(k);
  if (!set) {
    set = new Set<number>();
    m.set(k, set);
  }
  set.add(v);
}
function lists(k: string, v: string, idx: Map<string, string[]>): number {
  let list = idx.get(k);
  if (list === undefined) {
    list = [];
    idx.set(k, list);
    console.log("new", k);
  }
  list.push(v);
  return list.length;
}
add("a", 1);
add("a", 2);
console.log(m.get("a")!.size);
const idx = new Map<string, string[]>();
console.log(lists("x", "1", idx), lists("x", "2", idx));
