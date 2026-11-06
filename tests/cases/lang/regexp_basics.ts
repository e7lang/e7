// Regular expressions (D4): literals, flags, exec / test, lastIndex, match results
const date = /(?<year>\d{4})-(?<month>\d{2})-(?<day>\d{2})/;
console.log(date, String(date), `${date}`, date.source, date.flags);
console.log(date.global, date.ignoreCase, date.multiline, date.sticky, date.unicode, date.dotAll, date.hasIndices);
console.log(/a/gimsuyd.flags, /x/y.toString(), typeof date);

const m = date.exec("released on 2024-11-06, patched 2025-01-31");
console.log(m);
if (m) {
  console.log(m[0], m[1], m.length, m.index, m.input.length);
  console.log(m.groups?.year, m.groups?.month, m.groups?.day);
  const [all, y, mo, d] = m;
  console.log(all, y, mo, d, [...m].join("|"));
  const g = m.groups;
  if (g) console.log(g.year + "/" + g.month);
}
console.log(date.exec("no date here"));

// global and sticky: lastIndex moves
const word = /\w+/g;
const text = "one two  three";
for (;;) {
  const w = word.exec(text);
  if (w === null) break;
  console.log(w[0], w.index, word.lastIndex);
}
console.log(word.lastIndex);
const sticky = /\d/y;
console.log(sticky.test("1a2"), sticky.lastIndex, sticky.test("1a2"), sticky.lastIndex);
sticky.lastIndex = 2;
console.log(sticky.exec("1a2"), sticky.lastIndex);

// groups that do not participate are undefined
const opt = /(a)?(b)|(c)/;
console.log(opt.exec("b"), opt.exec("c"));
const r2 = opt.exec("c");
if (r2) console.log(r2[1] === undefined, r2.length);

// a literal is a new object at each evaluation
function counter(): RegExp {
  return /x/g;
}
const c1 = counter();
c1.lastIndex = 5;
console.log(c1.lastIndex, counter().lastIndex, c1 === counter());

// flags
console.log(/^b/m.test("a\nb"), /^b/.test("a\nb"), /a.b/s.test("a\nb"), /a.b/.test("a\nb"));
console.log(/ABC/i.test("xabcx"), /\u{1F600}/u.test("😀"), /^.$/u.test("😀"), /^.$/.test("😀"));

// in objects, arrays and JSON
const holder = { pattern: /[a-z]+/gi, list: [/1/, /2/m] };
console.log(holder, JSON.stringify(holder), JSON.stringify(m));
