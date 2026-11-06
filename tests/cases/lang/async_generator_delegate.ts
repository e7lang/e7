// @requires-vm: 0.3.1
// yield* in async generators: values, next(v) arguments, return(v) and throw(e) are forwarded to the
// inner async iterator; its return value is the value of the yield* expression
async function* inner(name: string): AsyncGenerator<string, string, number> {
  try {
    const a = yield name + ":1";
    console.log(name, "got", a);
    const b = yield name + ":2";
    console.log(name, "got", b);
    return name + ":result";
  } catch (e) {
    console.log(name, "caught", e instanceof Error ? e.message : "?");
    yield name + ":after-catch";
    return name + ":caught-result";
  } finally {
    console.log(name, "finally");
  }
}

async function* outer(): AsyncGenerator<string, string, number> {
  yield "outer:start";
  const r = yield* inner("in");
  console.log("yield* gave", r);
  const s = yield* inner("in2");
  console.log("yield* gave", s);
  return "outer:done";
}

class Pair {
  async *[Symbol.asyncIterator](): AsyncGenerator<number> {
    yield 1;
    yield 2;
  }
}

async function* flat(): AsyncGenerator<number> {
  yield* new Pair();
  yield 3;
  yield* new Pair();
}

async function* chain(depth: number): AsyncGenerator<number> {
  yield depth;
  if (depth > 0) yield* chain(depth - 1);
}

function tick(name: string, n: number): void {
  let p = Promise.resolve();
  for (let i = 1; i <= n; i++) p = p.then(() => console.log(name, i));
}

async function main(): Promise<void> {
  tick("tick", 30);
  const g = outer();
  console.log(await g.next(0));
  console.log(await g.next(10));
  console.log(await g.next(11));
  console.log(await g.next(12));
  console.log(await g.next(13));
  console.log(await g.next(14));
  console.log(await g.next(15));
  console.log(await g.next(16));

  // return(v) while delegating: the inner generator's finally runs, then the outer returns v
  const h = outer();
  await h.next(0);
  await h.next(0);
  tick("tock", 12);
  console.log(await h.return("cancelled"));
  console.log(await h.next(0));

  // throw(e) while delegating: forwarded to the inner generator, which catches it
  const k = outer();
  await k.next(0);
  await k.next(0);
  console.log(await k.throw(new Error("injected")));
  console.log(await k.next(1));
  console.log(await k.next(2));

  const xs: number[] = [];
  for await (const x of flat()) xs.push(x);
  console.log(xs);
  const ys: number[] = [];
  for await (const y of chain(4)) ys.push(y);
  console.log(ys);
  // breaking out of a loop over a delegating generator closes both
  const loop: AsyncIterable<string> = outer();
  for await (const v of loop) {
    console.log("loop", v);
    if (v === "in:1") break;
  }
}
main().then(() => console.log("done"));
