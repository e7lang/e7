interface User { name: string; age: number; email?: string; tags: string[]; }
const u: User = { name: "Ann", age: 30, tags: ["a", "b"] };
const v: User = { name: "Bob", age: 25, email: "bob@x.com", tags: [] };
console.log(u.name, u.age, u.email, v.email, u.tags.length);
console.log(JSON.stringify(u), JSON.stringify(v));
u.age++;
u.email = "ann@x.com";
console.log(JSON.stringify(u));
type Pair = [number, string];
const p: Pair = [1, "one"];
const [num, word] = p;
console.log(num, word, p[0], p[1]);
function divmod(a: number, b: number): [number, number] { return [Math.floor(a / b), a % b]; }
const [q, r] = divmod(17, 5);
console.log(q, r);
const point = { x: 1, y: 2 };
const { x, y } = point;
console.log(x + y, JSON.stringify(point));
let a1 = 1, b1 = 2;
[a1, b1] = [b1, a1];
console.log(a1, b1);
interface Config { host: string; port: number; secure: boolean; retry: { count: number; delay: number } }
const cfg: Config = { host: "localhost", port: 8080, secure: false, retry: { count: 3, delay: 100 } };
console.log(JSON.stringify(cfg));
console.log(JSON.stringify(cfg, null, 2));
const parsed = JSON.parse('{"host":"h","port":1,"secure":true,"retry":{"count":5,"delay":7},"extra":1}') as Config;
console.log(parsed.host, parsed.port, parsed.secure, parsed.retry.count + parsed.retry.delay);
const list = JSON.parse("[1, 2.5, -3e2]") as number[];
console.log(list.length, list[2]);
const people: User[] = [u, v];
console.log(people.map(p => p.name).join("&"), people.filter(p => p.age > 26).length);
function describe(o: { name: string; age: number }): string { return `${o.name}/${o.age}`; }
console.log(describe(u), describe({ name: "Zed", age: 1 }));
console.log(Object.keys(cfg).join(","), JSON.stringify({ a: [1, 2], c: "x\ny" }));
console.log(JSON.stringify(["a", "b"]), JSON.stringify("é😀"), JSON.stringify({ n: NaN, i: Infinity }));
const maybe = JSON.parse('{"a":null,"b":"x"}') as { a: string | null; b: string | null };
console.log(maybe.a === null, maybe.b);
