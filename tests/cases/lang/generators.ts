function* count(n: number): Generator<number> {
  for (let i = 0; i < n; i++) yield i;
}
for (const x of count(3)) console.log(x);
const g = count(2);
console.log(g.next());
console.log(g.next());
console.log(g.next());
console.log(g.next());

function* acc(): Generator<number, number, number> {
  let total = 0;
  while (total < 10) {
    const v = yield total;
    total += v;
  }
  return total * 100;
}
const a = acc();
console.log(a.next(0).value);
console.log(a.next(4).value);
console.log(a.next(7));

function* withFinally(): Generator<string> {
  try {
    yield "a";
    yield "b";
    yield "c";
  } finally {
    console.log("cleanup");
  }
}
for (const s of withFinally()) {
  console.log(s);
  if (s === "b") break;
}
const w = withFinally();
console.log(w.next().value);
console.log(w.return(undefined));
console.log(w.next());

function* inner(): Generator<number, number> {
  yield 1;
  yield 2;
  return 3;
}
function* outer(): Generator<number> {
  const r = yield* inner();
  console.log("inner returned", r);
  yield* [10, 20];
}
for (const x of outer()) console.log(x);

class Range {
  constructor(private lo: number, private hi: number) {}
  *[Symbol.iterator](): Generator<number> {
    for (let i = this.lo; i < this.hi; i++) yield i;
  }
}
for (const x of new Range(5, 8)) console.log("r", x);

function* thrower(): Generator<number> {
  try {
    yield 1;
  } catch (e) {
    if (e instanceof Error) console.log("caught", e.message);
    yield 99;
  }
}
const t = thrower();
console.log(t.next());
console.log(t.throw(new Error("boom")));
console.log(t.next());
