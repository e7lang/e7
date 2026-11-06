// @runtime-error: nested too deeply to clone
interface Node { next: Node | null; v: number }
let head: Node | null = null;
for (let i = 0; i < 1000; i++) head = { next: head, v: i };
console.log(structuredClone(head)?.v);
