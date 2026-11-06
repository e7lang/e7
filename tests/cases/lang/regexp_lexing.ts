// '/' is a division after a value and starts a regular expression where an expression starts
const a = 10, b = 2;
const arr = [8, 4];
let c = 9;
c /= 3;
let i = 4;
const q = i++ / 2;
console.log(a / b / 2, (a) / b, arr[0] / arr[1], a /b/ 1, c, q, i);
console.log(/=/.test("a=b"), /\//.test("a/b"), /[/]/.test("/"), /a\/b/.source, `${/x+/g.source}!`);
const pick = (n: number): RegExp => (n > 0 ? /pos/ : /neg/);
function f(): RegExp {
  return /ret/i;
}
console.log(pick(1), pick(-1), f(), [/a/, /b/], { k: /c/ }, typeof /d/);
const t = !/x/.test("y") && /y/.test("y");
console.log(t, 1 + /abc/.source.length, "x".replace(/x/, "/"), /[\]/]+/.exec("a]/b"));
// comments are not patterns
const d = a / /* half */ b; // a / b
console.log(d);
