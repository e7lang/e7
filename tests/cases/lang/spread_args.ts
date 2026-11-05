// Spread arguments: tuples supply one argument per element; arrays (and tuples) can be spread into
// rest parameters anywhere in the argument list. Optional element access a?.[i].

function point3(x: number, y: number, label: string): string {
  return `${label}(${x}, ${y})`;
}
const args: [number, number, string] = [1, 2, "P"];
console.log(point3(...args));
const xy: [number, number] = [3, 4];
console.log(point3(...xy, "Q"));
const lbl: [string] = ["R"];
console.log(point3(5, 6, ...lbl));

// the tuple expression is evaluated once, at its position
let evals = 0;
const order: string[] = [];
function make(): [number, string] {
  evals++;
  order.push("make");
  return [7, "seven"];
}
function tag(s: string): number {
  order.push(s);
  return 0;
}
function three(a: number, b: number, c: string): string {
  return `${a}/${b}/${c}`;
}
console.log(three(tag("first"), ...make()), evals, order.join(","));

// optional parameters, boxing of primitives
function opt(a: number, b?: number, c?: string): string {
  return `${a} ${b} ${c}`;
}
const one: [number] = [1];
const two: [number, number] = [1, 2];
console.log(opt(...one), opt(...two), opt(...two, "c"));

// rest parameters: elements and spread arrays are collected into a new array
function total(label: string, ...nums: number[]): string {
  nums.push(0);  // the rest array is a fresh array: the caller's array is not changed
  return label + "=" + nums.reduce((s, n) => s + n, 0) + " (" + nums.length + ")";
}
const arr = [1, 2, 3];
console.log(total("a", ...arr), arr);
console.log(total("b", 10, ...arr, 20, ...arr));
console.log(total("c"), total("d", ...[]));
console.log(total(...lbl, ...xy));
const s = new Set<number>([4, 5]);
console.log(total("set", ...s));

// closures and methods
const fmt = (a: string, b: number): string => a.repeat(b);
const fa: [string, number] = ["ab", 3];
console.log(fmt(...fa));
class Vec {
  constructor(
    public x: number,
    public y: number,
  ) {}
  add(dx: number, dy: number): Vec {
    return new Vec(this.x + dx, this.y + dy);
  }
  static of(...cs: number[]): Vec {
    return new Vec(cs[0], cs[1]);
  }
}
const v = new Vec(...xy).add(...xy);
console.log(v.x, v.y, Vec.of(...arr).x);

// spreading tuples in array literals
const merged = [...xy, ...arr, 9];
console.log(merged);
const t2: [number, number] = [...xy];
console.log(t2);

// spread inside a loop: one hidden temporary per call site
const pts: [number, number, string][] = [
  [0, 0, "o"],
  [1, 1, "i"],
];
for (const p of pts) console.log(point3(...p));

// optional element access
const maybe: number[] | undefined = Math.random() < 2 ? [10, 20] : undefined;
const none: number[] | null = null;
console.log(maybe?.[1], none?.[0]);
const grid: (string[] | undefined)[] = [["a", "b"], undefined];
console.log(grid[0]?.[1], grid[1]?.[0]);
let idx = 0;
const next = (): number => idx++;
console.log(none?.[next()], idx, maybe?.[next()], idx);
const str: string | undefined = "hello";
console.log(str?.[1]);
const tup: [number, string] | null = [1, "one"];
console.log(tup?.[1]);
const nested: { list?: number[] } = { list: [5, 6] };
console.log(nested.list?.[0], ({} as { list?: number[] }).list?.[0]);
