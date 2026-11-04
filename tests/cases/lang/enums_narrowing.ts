enum Color { Red, Green = 5, Blue }
enum Dir { Up = "UP", Down = "DOWN" }
console.log(Color.Red, Color.Green, Color.Blue, Dir.Up, Dir.Down);
function name(c: Color): string {
  switch (c) {
    case Color.Red: return "red";
    case Color.Green: return "green";
    default: return "blue";
  }
}
console.log(name(Color.Green), name(Color.Blue));
function pickDir(): Dir { return Dir.Down; }
const d: Dir = pickDir();
console.log(d === Dir.Down, d === Dir.Up);
function len(s: string | null): number {
  if (s === null) return -1;
  return s.length;
}
console.log(len(null), len("abc"));
function first(xs: number[]): number | undefined { return xs.length ? xs[0] : undefined; }
const f = first([7]);
if (f !== undefined) console.log("first", f + 1);
const g = first([]);
console.log(g === undefined, g ?? 42, first([3]) ?? 0);
class TreeNode { left: TreeNode | null = null; right: TreeNode | null = null; constructor(public v: number) {} }
function insert(n: TreeNode | null, v: number): TreeNode {
  if (n === null) return new TreeNode(v);
  if (v < n.v) n.left = insert(n.left, v);
  else n.right = insert(n.right, v);
  return n;
}
function inorder(n: TreeNode | null, out: number[]): void {
  if (!n) return;
  inorder(n.left, out);
  out.push(n.v);
  inorder(n.right, out);
}
let root: TreeNode | null = null;
for (const v of [5, 3, 8, 1, 4, 9]) root = insert(root, v);
const out: number[] = [];
inorder(root, out);
console.log(out.join(" "));
function depth(n: TreeNode | null): number { return n ? 1 + Math.max(depth(n.left), depth(n.right)) : 0; }
console.log(depth(root));
let maybeStr: string | undefined;
console.log(maybeStr === undefined, maybeStr?.length);
maybeStr = "hey";
console.log(maybeStr.length, maybeStr?.toUpperCase());
const opt: { a?: number } = {};
console.log(opt.a ?? -1, opt.a === undefined);
const nums: (number | null)[] = [1, null, 3];
let tot = 0;
for (const n of nums) if (n !== null) tot += n;
console.log(tot);
class Holder { val: number | null = null; get2(): number { if (this.val === null) return 0; return this.val * 2; } }
const h = new Holder();
console.log(h.get2());
h.val = 21;
console.log(h.get2());
const words: string[] = ["a", "bb"];
const w = words.find(x => x.length === 2);
console.log(w?.length, words.find(x => x === "q")?.length);
