// third test round of 0.1.3 (t02_hoist_switch_try): must print what Node.js prints
function sw(n: number): number {
  switch (n) {
    case 1: {
      const x = 21;
      function a(): number { return x * 2; }
      return a();
    }
    case 2: {
      let y = 1;
      function b(): number { return y; }
      y = 7;
      return b();
    }
    default:
      return -1;
  }
}
console.log(sw(1), sw(2), sw(3));
function tf(): string {
  const out: string[] = [];
  try {
    let msg = "try";
    function p(): string { return msg; }
    out.push(p());
    msg = "changed";
    throw new Error(p());
  } catch (e) {
    const note = e instanceof Error ? e.message : "?";
    function q(): string { return note; }
    out.push(q());
  } finally {
    let fin = "fin";
    function r(): string { return fin; }
    fin += "!";
    out.push(r());
  }
  return out.join(",");
}
console.log(tf());
function rec(n: number): number {
  const seed = n * 100;
  if (n <= 0) return base();
  function base(): number { return seed; }
  return rec(n - 1) + inner();
  function inner(): number { return seed + 1; }
}
console.log(rec(3));
function* gen(): Generator<number> {
  let z = 1;
  function g(): number { return z; }
  yield g();
  z = 2;
  yield g();
  for (let i = 0; i < 2; i++) {
    function gi(): number { return i + z; }
    yield gi();
  }
}
console.log([...gen()]);
function outer(n: number): boolean {
  return isEven(n);
  function isEven(k: number): boolean { return k === 0 ? true : isOdd(k - 1); }
  function isOdd(k: number): boolean { return k === 0 ? false : isEven(k - 1); }
}
console.log(outer(10), outer(7));
