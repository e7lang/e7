// recursive interfaces (also mutually recursive and generic) and recursive object type aliases
interface Tree { label: string; children: Tree[] }
interface A { b: B | null; n: number }
interface B { a: A; s: string }
interface List<T> { head: T; tail: List<T> | null }
type Json = { key: string; kids: Json[]; parent?: Json | undefined };
interface Other { label: string; children: Other[] }
function size(t: Tree): number { return 1 + t.children.reduce((n, c) => n + size(c), 0); }
function len<T>(l: List<T> | null): number { return l === null ? 0 : 1 + len(l.tail); }
const t: Tree = { label: "root", children: [{ label: "a", children: [] }, { label: "b", children: [{ label: "c", children: [] }] }] };
const a: A = { b: null, n: 1 };
const b: B = { a, s: "x" };
a.b = b;
const l: List<number> = { head: 1, tail: { head: 2, tail: null } };
const j: Json = { key: "r", kids: [{ key: "k", kids: [] }] };
const o: Other = t;
console.log(size(t), a.b?.a.n, len(l), j.kids[0].key, o.children.length, JSON.stringify(t));
console.log(t);
