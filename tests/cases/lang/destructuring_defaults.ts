// Default values and rest elements in destructuring declarations, parameters and for...of.

// arrays: a default applies past the end of the array
const nums = [1, 2];
const [a = 10, b = 20, c = 30] = nums;
console.log(a, b, c);
const [first, ...others] = [5, 6, 7, 8];
console.log(first, others, others.length);
const [only, ...none] = [9];
console.log(only, none);
const [, second = "-", ...tail] = ["x", "y", "z"];
console.log(second, tail);

// defaults may refer to earlier bindings and are evaluated only when needed
let evaluated = 0;
const dflt = (v: number): number => {
  evaluated++;
  return v;
};
const [p = dflt(1), q = p * 2, r = dflt(3)] = [7];
console.log(p, q, r, evaluated);

// tuples
const tup: [string, number, boolean] = ["t", 2, true];
const [ts, ...trest] = tup;
console.log(ts, trest, trest[0], trest[1]);
const opt: [string, number | undefined] = ["o", undefined];
const [os, on = 42] = opt;
console.log(os, on);

// objects: a default applies when the property is undefined (absent optional property)
interface Options {
  host?: string;
  port?: number;
  tags?: string[];
  retry: { count?: number; delay: number };
}
const o1: Options = { port: 8080, retry: { delay: 5 } };
const {
  host = "localhost",
  port = 80,
  tags = ["none"],
  retry: { count = 3, delay },
} = o1;
console.log(host, port, tags, count, delay);

// renaming with a default, nested defaults
const { host: h = "h", retry: { count: n = 1 } = { count: 2 } } = { host: "given", retry: { count: undefined } } as {
  host?: string;
  retry?: { count?: number };
};
console.log(h, n);

// rest properties: the remaining properties (a new object)
const user = { id: 1, name: "Ann", email: "ann@example.com", admin: false };
const { id, ...profile } = user;
console.log(id, profile, Object.keys(profile));
profile.name = "changed";
console.log(user.name, profile.name, JSON.stringify(profile));
const { name: nm, ...rest2 } = profile;
console.log(nm, rest2);

class Point {
  constructor(
    public x: number,
    public y: number,
  ) {}
  len(): number {
    return Math.hypot(this.x, this.y);
  }
}
const { x: px, ...py } = new Point(3, 4);
console.log(px, py, JSON.stringify(py));

// parameters
function connect({ host = "127.0.0.1", port = 443 }: { host?: string; port?: number } = {}): string {
  return `${host}:${port}`;
}
console.log(connect(), connect({ port: 8443 }), connect({ host: "example.org" }));

function sum([x = 0, y = 0, ...more]: number[]): number {
  let s = x + y;
  for (const m of more) s += m;
  return s;
}
console.log(sum([]), sum([1]), sum([1, 2, 3, 4]));

const describe = ({ name, ...others }: { name: string; age: number; city: string }) => `${name}: ${JSON.stringify(others)}`;
console.log(describe({ name: "Bob", age: 30, city: "Oslo" }));

// for...of
const pairs: [string, number | undefined][] = [
  ["a", 1],
  ["b", undefined],
];
for (const [k, v = -1] of pairs) console.log(k, v);
const m = new Map<string, number>([
  ["x", 1],
  ["y", 2],
]);
for (const [k, ...vs] of m) console.log(k, vs);
const records = [{ label: "r1" }, { label: "r2", weight: 5 }] as { label: string; weight?: number }[];
for (const { label, weight = 1 } of records) console.log(label, weight);

// top-level destructuring with defaults in a function scope and in closures
function closures(): number[] {
  const [u = 1, w = 2] = [100];
  const fs = [() => u, () => w];
  return fs.map((f) => f());
}
console.log(closures());
