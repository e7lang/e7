// @feature: Recursive types
// @category: Object Types
// @status: supported
// @note: recursive interfaces (also mutually recursive and generic) and object type aliases; `JSON.parse(text) as T` into a recursive type comes with e7vm 0.3
interface Tree { label: string; children: Tree[] }
interface List<T> { head: T; tail: List<T> | null }
type Node = { name: string; next?: Node | undefined };
function size(t: Tree): number { return 1 + t.children.reduce((n, c) => n + size(c), 0); }
const t: Tree = { label: "root", children: [{ label: "leaf", children: [] }] };
const l: List<number> = { head: 1, tail: { head: 2, tail: null } };
const n: Node = { name: "a", next: { name: "b" } };
console.log(size(t), l.tail?.head, n.next?.name);
