// console.log line breaking like Node.js: one line when the entries fit in 80 columns, otherwise one entry per line;
// arrays with more than 6 entries are grouped into aligned columns (numbers right-aligned); more than 100 items,
// nesting deeper than 2 levels and long strings are abbreviated.

function range(n: number, f: (i: number) => number): number[] {
  const out: number[] = [];
  for (let i = 0; i < n; i++) out.push(f(i));
  return out;
}

// short arrays stay on one line, longer ones are grouped
console.log([1, 2, 3, 4, 5, 6]);
console.log([1, 2, 3, 4, 5, 6, 7]);
console.log(range(26, (i) => i));
console.log(range(30, (i) => i * i * i));
console.log(range(12, (i) => (i % 3 === 0 ? -i * 1000.5 : i)));
console.log(range(101, (i) => i));
console.log(range(250, (i) => i % 7));
const bigs: bigint[] = [];
for (let i = 0; i < 9; i++) bigs.push(BigInt(i) ** 20n);
console.log(bigs);
const mixed: (number | null)[] = [1, null, 22, null, 333, null, 4444, 5];
console.log(mixed);

// strings are left-aligned; wide characters take two columns
console.log(["a", "bb", "ccc", "dddd", "eeeee", "ffffff", "g", "hh", "iii", "jjjj"]);
console.log(["apple", "banana", "cherry", "date", "elderberry", "fig", "grape", "honeydew", "kiwi", "lemon"]);
console.log(["中文", "日本語", "한국어", "abc", "😀😀", "e\u0301", "x", "Ａ", "zz", "q"]);
const words: string[] = [];
for (let i = 0; i < 40; i++) words.push("w".repeat(i % 9));
console.log(words);
// entries of very different lengths are not grouped
console.log(["a", "b", "c", "d", "e", "f", "a much longer string than all the others put together, really"]);
console.log([true, false, true, false, true, false, true, false]);

// objects: one line up to 80 columns
console.log({ alpha: 1, beta: 2, gamma: 3, delta: 4, epsilon: 5, zeta: 6, eta: 7, theta: 8 });
console.log({ alpha: "aaaaaaaaaa", beta: "bbbbbbbbbb", gamma: "cccccccccc", delta: "dddddddddd", e: 1 });
console.log({ alpha: "aaaaaaaaaa", beta: "bbbbbbbbbb", gamma: "cccccccccc", delta: "dddddddddd", e: 12 });
interface Tagged { id: number; name: string; tags: string[] }
const tagged: Tagged[] = [{ id: 1, name: "one", tags: ["x", "y"] }, { id: 2, name: "two", tags: [] }];
console.log(tagged);
console.log({ list: range(20, (i) => i), nested: { a: [1, 2, 3], b: { c: "d" } } });

// nesting deeper than 2 levels
const deepEmpty: number[][][][] = [[[[]]]];
console.log([[[[1]]]], deepEmpty, { a: { b: { c: { d: 1 } } } }, { a: { b: { c: [deepEmpty] } } });
console.log({ level1: { level2: { level3: { level4: "deep" } }, arr: [[["x"]]] } });
const grid: number[][] = [];
for (let r = 0; r < 8; r++) grid.push(range(8, (c) => r * 8 + c));
console.log(grid);
console.log([grid]);

// long strings: nested ones with line breaks are split into pieces joined with " +"
const text = "first line of text\nsecond line of text\nthird line, the last one";
console.log([text]);
console.log({ description: text, short: "a\nb" });
console.log({ long: "x".repeat(70), longer: "y".repeat(90) });
console.log(["line one is long enough\n", "\n\n\n", "ends with a newline but is long\n"]);
console.log(["s".repeat(10001)], { t: "t".repeat(10000) + "\n" + "u".repeat(5) });

// several arguments are separated by spaces, strings stay unquoted
console.log("label:", [1, 2], { a: "b" }, "end", 3);
console.log();
console.log("");
console.log("", "");
