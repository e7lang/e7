// flatMap / flat, Object.entries / values / assign, String replace / replaceAll with a function.

// flatMap: array results are flattened one level, other results are kept
const words = ["a b", "c", "", "d e f"];
console.log(words.flatMap((w) => w.split(" ")).join(","));
console.log([1, 2, 3].flatMap((x) => [x, x * 10]));
console.log([1, 2, 3].flatMap((x, i) => (x % 2 === 1 ? [x + i] : [])));
console.log([1, 2].flatMap((x) => x * 2));
console.log([[1], [2, 3]].flatMap((x) => [x]));
const nested = [1, 2].flatMap((x): number[][] => [[x], [x, x]]);
console.log(nested, nested.length);
const idx: string[] = [];
["x", "y"].flatMap((s, i, arr) => {
  idx.push(`${s}${i}/${arr.length}`);
  return [s];
});
console.log(idx);
// the returned arrays are copied
const inner = [7];
const fm = [0].flatMap(() => inner);
fm.push(8);
console.log(inner, fm);

// flat with a constant depth
const lists = [[1, 2], [3], [], [4, 5, 6]];
console.log(lists.flat(), lists.flat().length);
const cube = [
  [
    [1, 2],
    [3, 4],
  ],
  [[5], [6, 7]],
];
console.log(cube.flat(), cube.flat(2).join(), cube.flat(Infinity).join(), cube.flat(0));
console.log([1, 2, 3].flat(), ["s"].flat(5));
const strs = [["a"], ["b", "c"]].flat();
console.log(strs.join("-"));

// Object.values / Object.entries: properties of one type, in the object's own order
const scores = { ann: 3, bob: 7, cy: 5 };
console.log(Object.values(scores), Object.entries(scores));
let total = 0;
for (const [name, score] of Object.entries(scores)) {
  console.log(name, score);
  total += score;
}
console.log(total, Object.values(scores).reduce((a, b) => a + b, 0));
interface Labels {
  title: string;
  subtitle?: string;
  footer: string;
}
const labels: Labels = { footer: "f", title: "t" };
console.log(Object.values(labels), Object.entries(labels));
const withSub: Labels = { title: "T", subtitle: "S", footer: "F" };
console.log(Object.values(withSub).join("|"));
class Dims {
  constructor(
    public w: number,
    public h: number,
  ) {}
  area(): number {
    return this.w * this.h;
  }
}
console.log(Object.values(new Dims(2, 3)), Object.entries(new Dims(4, 5)));
const flags = { a: true, b: false };
console.log(Object.entries(flags).filter(([, v]) => v).map(([k]) => k));

// Object.assign: an object literal target, or a target that has every source property
const defaults = { host: "localhost", port: 80, tls: false };
const overrides = { port: 443, tls: true };
const merged = Object.assign({}, defaults, overrides);
console.log(merged);
const merged2 = Object.assign({ extra: 1 }, defaults);
console.log(merged2, merged2.extra + merged2.port);
interface Settings {
  host: string;
  port: number;
  tls: boolean;
  name?: string;
}
const settings: Settings = { host: "h", port: 1, tls: false };
const same = Object.assign(settings, overrides);
console.log(JSON.stringify(settings), same === settings);
const partial: { name?: string; port?: number } = { port: 8080 };
Object.assign(settings, partial);
console.log(JSON.stringify(settings));
Object.assign(settings, { name: "srv" } as { name?: string });
console.log(settings, JSON.stringify(settings));
class Point {
  x = 0;
  y = 0;
}
const p = Object.assign(new Point(), { y: 5 });
console.log(p, p instanceof Point);

// replace / replaceAll with a replacement function
const text = "a-b-c";
console.log(text.replace("-", (m) => `[${m}]`));
console.log(text.replaceAll("-", (m, offset) => `(${offset})`));
console.log("xyz".replace("q", () => "never"), "xyz".replaceAll("q", () => "never"));
console.log("aaa".replaceAll("aa", (m, i, s) => `${m.toUpperCase()}${i}${s.length}`));
console.log("abc".replaceAll("", (m, i) => `${i}`));
console.log("abc".replace("", () => "^"));
console.log("$1 $&".replaceAll("$", () => "$$"));
let n = 0;
console.log("1+1+1".replaceAll("+", () => String(++n)), n);
const upper = (s: string): string => s.toUpperCase();
console.log("one two".replace("two", upper));
try {
  "boom".replace("o", () => {
    throw new RangeError("from replacer");
  });
} catch (e) {
  console.log(String(e));
}
