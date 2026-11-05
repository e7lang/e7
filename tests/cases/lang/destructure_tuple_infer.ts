// an array literal destructured by an array pattern is a tuple when its elements have different types
const [a, b] = [1, "x"];
console.log(a + 1, b.toUpperCase());
let [c, d, e] = [true, 2.5, "s"];
c = !c;
d *= 2;
e += "!";
console.log(c, d, e);
const [[f, g], h] = [[1, "y"], false];
console.log(f, g, h);
const [i, ...rest] = [1, "z", true];
console.log(i, rest[0], rest[1], rest.length);
const [j, k = "dflt"] = [3, "given"];
console.log(j, k);
const [, m] = ["skip", 9];
console.log(m);
const [n, o] = [1, 2];
console.log(n + o);
const [p, q, r = 7] = [1, 2];
console.log(p, q, r);
const [s, t] = [[1, 2], "w"];
console.log(s.length, t);
function pair(): void {
  const [x, y] = [[0.5], new Map<string, number>([["k", 1]])];
  console.log(x[0], y.get("k"));
}
pair();
for (const [u, v] of [[1, 2], [3, 4]]) console.log(u * v);
