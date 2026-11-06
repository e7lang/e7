// String methods with regular expressions: match, matchAll, replace, replaceAll, split, search
const s = "Alice: 30, Bob: 25, Carol: 41";

console.log(s.match(/\d+/g), s.match(/(\w+): (\d+)/), s.match(/zzz/), s.match(/zzz/g));
const first = s.match(/(?<name>\w+): (?<age>\d+)/);
console.log(first?.index, first?.groups?.name, first?.input === s);

for (const e of s.matchAll(/(?<name>\w+): (?<age>\d+)/g)) console.log(e.index, e[1], e.groups?.age);
console.log([...s.matchAll(/\d/g)].length, Array.from("a1b22".matchAll(/\d+/g)).map((x) => x[0]));
const it = "xyx".matchAll(/x/g);
console.log(it.next(), it.next(), it.next());

// replacement patterns
console.log(s.replace(/(\w+): (\d+)/, "$2 <- $1"));
console.log(s.replace(/(\w+): (\d+)/g, "[$&]"), s.replace(/\d+/g, "$$"));
console.log("abc".replace(/b/, "$`|$'"), "abc".replace(/(b)/, "$1$1$2$0$10"));
console.log(s.replace(/(?<name>\w+): (?<age>\d+)/g, "$<age>:$<name>$<none>"), "ab".replace(/(a)/, "$<x>"));
console.log("aaa".replaceAll(/a/g, "b"), "a.b.c".replace(/\./g, "-"), "x".replace(/(y)?x/, "[$1]"));
const twelve = "1 2 3 4 5 6 7 8 9 10 11 12";
console.log(twelve.replace(/(\d)(\d)?/g, "<$1|$2>"), "abcdefghijk".replace(/(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)/, "$11-$10-$1"));

// replacement functions: (match, groups..., offset, input[, groups])
console.log(s.replace(/\d+/g, (n) => String(Number(n) + 1)));
console.log(s.replace(/(\w+): (\d+)/g, (all, name, age, offset) => `${name.toUpperCase()}=${age}@${offset}`));
console.log(s.replace(/(?<name>\w+): (?<age>\d+)/g, (all, n, a, off, input, groups) => `${groups.age}/${input.length}`));
console.log("a-b".replace(/(x)?-/, (m, x) => `{${x}}`), "aXbX".replaceAll(/X/g, (m, off) => String(off)));

// split
console.log("a, b,c ,d".split(/\s*,\s*/), "2024-11-06".split(/-/), "a1b2c3".split(/\d/));
console.log("a1b2c3".split(/(\d)/), "abc".split(/(?:)/), "".split(/x/), "".split(/(?:)/), "abc".split(/b/, 1));
console.log("one  two".split(/(\s)(\s)?/), "test".split(/(?=s)/), "😀😁".split(/(?:)/u), "😀😁".split(/(?:)/));
console.log("a b c".split(/ /, 0), "a b c".split(/ /, -1), "xAyAz".split(/a/i));

// search keeps lastIndex
const g = /o/g;
g.lastIndex = 3;
console.log("foo boo".search(/b/), "foo".search(/x/), "foo".search(g), g.lastIndex);

// replaceAll and matchAll need g: catchable TypeErrors, as in JavaScript
try {
  "abc".replaceAll(/b/, "x");
} catch (e) {
  if (e instanceof TypeError) console.log("TypeError:", e.message);
}
try {
  for (const x of "abc".matchAll(/b/)) console.log(x);
} catch (e) {
  if (e instanceof TypeError) console.log("TypeError:", e.message);
}

// string patterns keep working
console.log("a.b.c".split("."), "aXa".replace("a", "$&$&"), "aXa".replaceAll("a", "b"), "x-y".replace("-", () => "+"));
