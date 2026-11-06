// destructuring assignment statements: object and array patterns with defaults, nesting, rest, and property targets
interface Opts { host: string; port?: number | undefined; tags: string[]; nested: { depth: number; name?: string | undefined } }
let host = "", port = 0, first = "", depth = 0, name = "";
let rest: string[] = [];
const o: Opts = { host: "h", tags: ["a", "b", "c"], nested: { depth: 3 } };
({ host, port = 80, tags: [first, ...rest], nested: { depth, name = "anon" } } = o);
console.log(host, port, first, rest, depth, name);
const box = { x: 0, y: 0 };
({ x: box.x, y: box.y } = { x: 5, y: 6 });
let a: number | undefined = 1, b = 2, c = 0;
const nums: (number | undefined)[] = [7, undefined, 8, 9];
let more: (number | undefined)[] = [];
[a, b = 10, ...more] = nums;
console.log(box, a, b, more);
let p = 0, q = 0;
[p, [q, c]] = [1, [2, 3]];
let others: { k: number } = { k: 0 };
let id = 0;
({ id, ...others } = { id: 9, k: 4 });
console.log(p, q, c, id, others);
