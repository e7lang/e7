// Edge cases of RegExpBuiltinExec and the String methods: lastIndex conversions, sticky matching, empty
// matches, groups that did not participate
const g = /a/g;
g.lastIndex = -5;
console.log(g.test("ba"), g.lastIndex);
g.lastIndex = 1.5;
console.log(g.lastIndex, g.exec("aaa"), g.lastIndex);
const y = /b/y;
console.log("ab".match(y), y.lastIndex, "ab".replace(y, "X"), y.lastIndex);
y.lastIndex = 1;
console.log("ab".replace(y, "X"), y.lastIndex, "abab".split(/b/y));
const m = "x1y2".match(/\d/g);
console.log(m?.index, m?.input, m?.length, JSON.stringify(m));
const e = /(\d)(?<n>\d)?/.exec("5");
console.log(e?.[2] === undefined, `${e?.[2]}`, "v=" + e?.[2], e?.groups);
console.log("aaa".replace(/a*?/g, "-"), "😀😀".replace(/(?:)/gu, "-"), "😀".replace(/(?:)/g, "-").length);
console.log("Test".replace(/t/gi, (m, off) => `${m}${off}`), "a-b_c".split(/[-_]/), "abc".match(/(?<x>z)?c/)?.groups);
const it = "a1a2".matchAll(/a(\d)/g);
const first = it.next();
console.log(first.done, first.value?.[1], [...it].length);
