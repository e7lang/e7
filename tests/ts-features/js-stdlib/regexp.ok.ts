// @feature: `RegExp`
// @category: JavaScript: Standard Library
// @status: partial
// @note: ES2024 regular expressions without the `v` flag (an own backtracking engine whose steps count against the budget): `test` / `exec` / `lastIndex`, `match` / `matchAll` / `replace` / `replaceAll` / `split` / `search`; the named groups of a literal are typed
const re = new RegExp("(\\d+)-(\\d+)");
const m = re.exec("10-20");
console.log(m !== null ? m[1] : "", m?.index);
const date = /(?<year>\d{4})-(?<month>\d{2})/;
console.log(date.exec("on 2024-11")?.groups?.year, "a1b22".replace(/\d+/g, (d) => `<${d}>`));
for (const x of "k=v; a=b".matchAll(/(\w)=(\w)/g)) console.log(x[1], x[2], x.index);
console.log("a, b ,c".split(/\s*,\s*/), "x".search(/x/), /^\p{Lu}/u.test("Émile"));
