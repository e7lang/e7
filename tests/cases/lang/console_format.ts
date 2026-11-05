// console.log with a format string first, like Node.js: %s %d %i %f %j %o %O %c %% substitutions.

class Item {
  name: string;
  qty: number;
  constructor(name: string, qty: number) {
    this.name = name;
    this.qty = qty;
  }
}
const item = new Item("pen", 3);
type Deep = [number, [number, [number, number[]]]];
const deep: Deep = [1, [2, [3, [4]]]];
const nested = { a: { b: { c: [1, 2] } }, list: deep };

// %s: strings and primitives as String(x), -0 and bigints like inspect, objects inspected one level deep
console.log("%s|%s|%s|%s|%s|%s", "text", 42, -0, 7n, true, null);
console.log("%s and %s", deep, nested);
console.log("%s", item, new Map<string, number>([["k", 1]]), new Set<string>(["x"]));
console.log("%s %s", new Date(0), new Uint8Array([1, 2, 3]));
const nothing: string | undefined = undefined;
console.log("%s!", nothing);

// %d / %i / %f: numbers
console.log("%d %d %d %d %d %d", 42, -0, 1.5, "0x1f", "  12  ", "abc");
console.log("%d %d %d %d", true, null, [7], 12n);
console.log("%i %i %i %i %i", 42.9, -0.5, "12px", 1e21, 5n);
console.log("%f %f %f %f", "3.25abc", 1e-7, "x", 9n);

// %j: JSON
const row: [number, string, string | null] = [1, "two", null];
console.log("%j", { a: row, b: { c: true } });
console.log("%j %j %j", "str", 1.5, nothing);
class Link {
  id = 1;
  next: Link | null = null;
}
const link = new Link();
link.next = link;
console.log("cyclic: %j", link);

// %O: inspect (strings quoted); %o: inspect with hidden properties, 4 levels deep
console.log("%O / %O", "quoted", nested);
console.log("%o", nested);
const tuple: [number, string, boolean[]] = [1, "a", [true]];
console.log("%o", tuple);
console.log("%o %o", new Uint8Array([1, 2]), new Uint8Array(0).buffer);
console.log("%o", { m: new Map<string, number[]>([["k", [1]]]), d: new Date(0), i: item });

// %c is dropped, %% is a percent sign, unknown or unmatched placeholders stay
console.log("%cstyled%c text", "color: red", "font-weight: bold");
console.log("100%% sure, %s", "really");
console.log("100%%");
console.log("%s %s %s", "only one");
console.log("%x %y %", 1, 2);
console.log("%", "a");
console.log("a%sb%sc", 1, 2, 3, "extra", [4]);
console.log("%s%s", "no", "space", "after");
console.log("trailing %");
console.log("%%s %s", "x");

// the first argument is a format string only when it is a string
console.log(5, "%s", 6);
console.log([1], "%d");
