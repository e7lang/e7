// Patterns built at run time: new RegExp, catchable SyntaxErrors, source escaping, copies
function escapeRegExp(s: string): string {
  return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}
const words = ["a.b", "c+d", "(e)"];
const any = new RegExp(words.map(escapeRegExp).join("|"), "g");
console.log(any, any.source, any.flags);
console.log("x a.b y (e) c+d axb".match(any));
console.log("x a.b y".replace(any, (m) => m.toUpperCase()));

const re = new RegExp("(\\w+)@(\\w+)\\.com");
const m = re.exec("mail bob@example.com now");
console.log(m, m?.[1], m?.[2], m?.index);
console.log(new RegExp("a/b"), new RegExp("[/]"), new RegExp("\n"), new RegExp(""), new RegExp("", "g").source);
console.log(String(new RegExp("x", "yigsmd")), new RegExp(/abc/gi), new RegExp(/abc/gi, "m"), new RegExp(re).source);

for (const [src, flags] of [["(", ""], ["a**", ""], ["[b-a]", ""], ["\\p{Foo}", "u"], ["a", "gg"], ["a", "x"], ["(?<n>a)\\k<m>", ""]]) {
  try {
    const r = new RegExp(src, flags);
    console.log("ok", r);
  } catch (e) {
    if (e instanceof SyntaxError) console.log(e.name, e.message);
  }
}

// a pattern from the outside, counted
function count(text: string, pattern: string): number {
  return text.match(new RegExp(pattern, "g"))?.length ?? 0;
}
console.log(count("banana", "an"), count("banana", "x"), count("aaaa", "aa"), count("", ""));

// case-insensitive and unicode
const greek = new RegExp("σ+", "iu");
console.log(greek.test("ΣΣς"), new RegExp("\\p{Lu}+", "u").exec("abcDEFg"), new RegExp("\\u{1F600}", "u").test("😀"));
console.log("ǅ".match(/ǆ/i), "ſ".match(/s/i), "ſ".match(/s/iu), "K".match(/k/iu));
console.log("x😀y".split(/(?:)/u), "😀".match(/./gu), "😀".match(/./g)?.length);
