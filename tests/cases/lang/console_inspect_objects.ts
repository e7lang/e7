// console.log of Map, Set, Uint8Array, ArrayBuffer, Date, tuples, class instances and cyclic structures, as Node.js.

class Point {
  x: number;
  y: number;
  constructor(x: number, y: number) {
    this.x = x;
    this.y = y;
  }
  norm(): number { return Math.hypot(this.x, this.y); }
}
class Point3 extends Point {
  z: number = 0;
  label?: string;
  constructor(x: number, y: number, z: number) {
    super(x, y);
    this.z = z;
  }
}
class Empty {}
class Holder {
  items: Point[] = [];
  byName = new Map<string, Point>();
  tags = new Set<string>();
}

console.log(new Point(1, 2), new Point3(1, 2, 3), new Empty(), [new Empty()]);
const h = new Holder();
console.log(h);
h.items.push(new Point(0, 0), new Point3(-1, 0.5, 2));
h.byName.set("origin", h.items[0]);
h.tags.add("a").add("b");
console.log(h);
console.log({ deep: { deeper: { deepest: new Point(5, 6) } } }, [[[new Empty()]]], [[[new Point(1, 1)]]]);

// tuples print as arrays
const pair: [string, number] = ["a", 1];
const triple: [number, [boolean, string], Point] = [1, [true, "x"], new Point(0, 1)];
console.log(pair, triple, [pair, pair]);

// Map and Set
const m = new Map<string, number[]>();
console.log(m, new Set<number>());
m.set("one", [1]).set("two", [1, 2]).set("it's", []);
console.log(m);
const nums = new Map<number, string>();
for (let i = 0; i < 12; i++) nums.set(i * 1.5, "v" + i);
console.log(nums);
const objKeys = new Map<Point, Map<string, boolean>>([[new Point(1, 2), new Map<string, boolean>([["ok", true]])]]);
console.log(objKeys, [objKeys], [[objKeys]]);
const s = new Set<number>([3, 1, -0, 0, NaN, NaN, 2]);
console.log(s, new Set<string>(["a b", "c'd"]), new Set<bigint>([1n, 2n]));
const many = new Set<number>();
for (let i = 0; i < 105; i++) many.add(i);
console.log(many);
const bigMap = new Map<number, number>();
for (let i = 0; i < 102; i++) bigMap.set(i, i * i);
console.log(bigMap.size, [bigMap].length);

// Uint8Array and ArrayBuffer
console.log(new Uint8Array(0), new Uint8Array([1, 2, 255]));
const bytes = new Uint8Array(40);
for (let i = 0; i < bytes.length; i++) bytes[i] = (i * 37) & 255;
console.log(bytes);
console.log(new Uint8Array(130));
console.log(bytes.subarray(3, 9), [bytes.subarray(0, 2)], { b: bytes.subarray(38) });
console.log(new ArrayBuffer(0), new ArrayBuffer(3), bytes.subarray(1, 3).buffer);
console.log(new Uint8Array(101).buffer);
console.log({ a: { b: { u: new Uint8Array(1), ab: new ArrayBuffer(1), m: new Map<string, number>([["k", 1]]), s: new Set<number>([1]) } } });
console.log({ a: { b: { u: new Uint8Array(0), ab: new ArrayBuffer(0), m: new Map<string, number>(), s: new Set<number>() } } });

// Date
console.log(new Date(0), new Date(1700000000123), new Date(NaN));
console.log([new Date(-62198755200000), new Date(8.64e15), new Date(-8.64e15)], { at: new Date(86400000) });

// cycles: <ref *n> marks the target of [Circular *n]
class Node {
  value: number;
  next: Node | null = null;
  children: Node[] = [];
  constructor(value: number) {
    this.value = value;
  }
}
const a = new Node(1);
const b = new Node(2);
a.next = b;
b.next = a;
console.log(a);
console.log([a, b]);
a.children.push(a, b);
console.log(a);
const self = new Node(0);
self.next = self;
console.log({ self }, [self, self]);
const ring: Node[] = [];
for (let i = 0; i < 4; i++) ring.push(new Node(i));
for (let i = 0; i < 4; i++) ring[i].next = ring[(i + 1) % 4];
console.log(ring[0]);
console.log(ring);
const cycMap = new Map<string, Node>([["a", a]]);
console.log(cycMap);
