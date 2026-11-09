// hoisted nested functions read the variables of their block declared after them (the same binding,
// a new one each time a loop body runs)
{
  const u = [1, 2];
  function f(): number { return u.length; }
  console.log(f());
  let z = 5;
  function q(): number { return z; }
  z = 6;
  console.log(q());
}
function g(): void {
  const v = [1, 2, 3];
  function h(): number { return v.length; }
  console.log(h());
}
g();
{
  const fs: (() => number)[] = [];
  for (let i = 0; i < 3; i++) {
    const k = i * 10;
    fs.push(() => k);
    function g(): number { return k + 1; }
    fs.push(g);
    let m = k;
    fs.push(() => m);
    m += 5;
  }
  let n = 0;
  while (n < 2) { const [a, b] = [n, n + 1]; fs.push(() => a + b); function h(): number { return a * 100; } fs.push(h); n++; }
  console.log(fs.map((f) => f()));
  function outer(): (() => number)[] {
    const out: (() => number)[] = [];
    for (const x of [1, 2]) {
      out.push(get);
      const y = x * 2;
      function get(): number { return y; }
    }
    return out;
  }
  console.log(outer().map((f) => f()));
}
