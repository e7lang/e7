type Method = "GET" | "POST";
type Dice = 1 | 2 | 3 | 4 | 5 | 6;
type Flag = true;
const m: Method = "GET";
let n: Dice = 4;
n = 6;
const f: Flag = true;
const ms: Method[] = ["GET", "POST"];
function send(method: Method, path: string): string { return method + " " + path; }
console.log(send(m, "/a"), send("POST", "/b"), n + 1, f, ms.join(","));
const k = "POST";
console.log(send(k, "/c"));
interface Req { method: Method; retries: 0 | 1 | 2 }
const r: Req = { method: "GET", retries: 2 };
console.log(r, typeof r.method, r.method.toLowerCase(), r.retries * 10);
function pick(x: number): Dice { return x > 3 ? 6 : 1; }
console.log(pick(5), pick(1));
let s: string = m;
s = s + "!";
console.log(s, m === "GET", m.length);
