// The expected output (.out) is ECMAScript 2025 as in Node.js 24: Node.js 22 implements the earlier closing rules
// (SPEC section 5.2), so this case is not compared with the Node.js that runs the tests.
// yield* of sync iterables in async generators: throw() and return() in every state, rejected promises
// among the values (an array is not closed, a sync generator is: its finally block runs), a sync
// generator that throws, catches a throw() or yields from its finally block, requests queued at once
function tick(name: string, n: number): void {
  let p = Promise.resolve();
  for (let i = 1; i <= n; i++) p = p.then(() => console.log(name, i));
}

function show(name: string, p: Promise<IteratorResult<string, void>>): Promise<void> {
  return p.then(r => console.log(name, r.value, r.done),
                (e: unknown) => console.log(name, "rejected", e instanceof Error ? e.name + ": " + e.message : "?"));
}

async function* overArray(xs: Promise<string>[]): AsyncGenerator<string> {
  try {
    yield "start";
    yield* xs;
    yield "after";
  } finally {
    console.log("overArray finally");
  }
}

function* letters(tag: string): Generator<string> {
  try {
    yield tag + "1";
    yield tag + "2";
    yield tag + "3";
  } finally {
    console.log(tag, "letters finally");
  }
}

function* promising(tag: string): Generator<Promise<string>> {
  try {
    yield Promise.resolve(tag + "-ok");
    yield Promise.reject(new Error(tag + "-bad"));
    yield Promise.resolve(tag + "-never");
  } finally {
    console.log(tag, "promising finally");
  }
}

function* catching(): Generator<string> {
  for (let i = 0; i < 3; i++) {
    try {
      yield "c" + i;
    } catch (e) {
      console.log("catching caught", e instanceof Error ? e.message : "?");
      yield "recovered" + i;
    }
  }
}

function* stubborn(): Generator<string> {
  try {
    yield "s1";
    yield "s2";
  } finally {
    yield "from-finally";
    console.log("stubborn finally done");
  }
}

function* exploding(): Generator<string> {
  yield "e1";
  throw new RangeError("sync failure");
}

async function* wrap(gen: Generator<string> | null): AsyncGenerator<string> {
  try {
    if (gen) yield* gen;
    yield "wrap-end";
  } finally {
    console.log("wrap finally");
  }
}

async function* wrapP(gen: Generator<Promise<string>>): AsyncGenerator<string> {
  try {
    yield* gen;
  } finally {
    console.log("wrapP finally");
  }
}

async function main(): Promise<void> {
  // an array with a rejected promise: the request rejects, the array is not closed
  const bad: Promise<string> = Promise.reject(new Error("array-bad"));
  bad.then(() => {}, () => {});
  const a = overArray([Promise.resolve("p1"), bad, Promise.resolve("p3")]);
  tick("ta", 8);
  for (let i = 0; i < 4; i++) await show("a" + i, a.next());
  // throw() / return() while delegating to an array
  const b = overArray([Promise.resolve("b1"), Promise.resolve("b2")]);
  await show("b0", b.next());
  await show("b1", b.next());
  tick("tb", 6);
  await show("b-throw", b.throw(new Error("into array")));
  await show("b-next", b.next());
  const c = overArray([Promise.resolve("c1"), Promise.resolve("c2")]);
  await show("c0", c.next());
  await show("c1", c.next());
  tick("tc", 6);
  await show("c-return", c.return(undefined));
  await show("c-next", c.next());
  // a sync generator yielding a rejected promise: it is closed (its finally runs) before the rejection
  const p = wrapP(promising("pr"));
  tick("tp", 8);
  for (let i = 0; i < 3; i++) await show("p" + i, p.next());
  // throw() caught by the sync generator, which continues
  const d = wrap(catching());
  await show("d0", d.next());
  tick("td", 6);
  await show("d-throw", d.throw(new Error("one")));
  await show("d1", d.next());
  // return() while the sync generator yields from its finally block
  const e = wrap(stubborn());
  await show("e0", e.next());
  tick("te", 8);
  await show("e-return", e.return(undefined));
  await show("e1", e.next());
  await show("e2", e.next());
  await show("e3", e.next());
  // the sync generator throws by itself
  const f = wrap(exploding());
  await show("f0", f.next());
  await show("f1", f.next());
  await show("f2", f.next());
  // requests queued at once (not started, then running)
  const g = wrap(letters("q"));
  const qs = [show("q-next", g.next()), show("q-throw", g.throw(new Error("queued"))), show("q-next2", g.next()),
              show("q-return", g.return(undefined)), show("q-next3", g.next())];
  tick("tq", 10);
  await Promise.all(qs);
  // return / throw before the start
  const h = wrap(letters("h"));
  await show("h-return", h.return(undefined));
  const k = wrap(letters("k"));
  await show("k-throw", k.throw(new Error("early")));
  // for await with break and with a throw over a delegating generator
  for await (const x of wrap(letters("lb"))) {
    console.log("loop", x);
    if (x === "lb2") break;
  }
  try {
    for await (const x of wrap(letters("lt"))) {
      console.log("loop", x);
      throw new Error("body");
    }
  } catch (err) {
    console.log("caught", err instanceof Error ? err.message : "?");
  }
}
main().then(() => console.log("done"));
