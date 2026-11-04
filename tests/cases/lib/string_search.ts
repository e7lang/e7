// Randomized differential test of substring search (indexOf/lastIndexOf/includes/split/replace)
let seed = 12345;
function rnd(n: number): number {
  seed = (seed * 1103515245 + 12345) % 2147483648;
  return seed % n;
}
function randStr(len: number, alphabet: string): string {
  let s = "";
  for (let i = 0; i < len; i++) s += alphabet[rnd(alphabet.length)];
  return s;
}
let acc = 0;
const lines: string[] = [];
for (let t = 0; t < 600; t++) {
  const alpha = t % 3 === 0 ? "ab" : t % 3 === 1 ? "abc" : "aab";
  const h = randStr(rnd(40), alpha);
  const n = randStr(rnd(9), alpha);
  const from = rnd(50) - 5;
  const i1 = h.indexOf(n), i2 = h.indexOf(n, from), l1 = h.lastIndexOf(n), l2 = h.lastIndexOf(n, from);
  acc = (acc * 31 + i1 * 7 + i2 * 13 + l1 * 17 + l2 * 19 + (h.includes(n, from) ? 1 : 0)) % 1000000007;
  if (n.length > 0) acc = (acc * 31 + h.split(n).length + h.replaceAll(n, "<$&>").length + h.replace(n, "[$`|$']").length) % 1000000007;
  if (t < 12) lines.push(`${JSON.stringify(h)} ${JSON.stringify(n)} ${from}: ${i1} ${i2} ${l1} ${l2} ${h.split(n).join("|")}`);
}
for (const l of lines) console.log(l);
console.log(acc);
// long periodic patterns (worst case for naive search)
const big = "a".repeat(20000) + "b";
const pat = "a".repeat(5000) + "b";
console.log(big.indexOf(pat), big.lastIndexOf(pat), big.indexOf("a".repeat(5000) + "c"), big.lastIndexOf("a".repeat(30)), big.includes(pat, 15001));
