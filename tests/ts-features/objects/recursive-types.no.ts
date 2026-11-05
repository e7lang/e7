// @feature: Recursive types
// @category: Object Types
// @status: partial
// @why: not implemented yet: recursive interfaces and type aliases
// @instead: a class for linked or tree-shaped data
// @error: recursive interfaces are not supported
interface Tree { label: string; children: Tree[] }
const t: Tree = { label: "root", children: [{ label: "leaf", children: [] }] };
console.log(t.children.length);
