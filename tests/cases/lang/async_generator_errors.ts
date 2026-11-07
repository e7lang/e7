// errors in async generators: thrown by the body, a yielded rejected promise, a rejected await,
// errors in finally blocks, errors from next() of a generator that is already done
async function* failing(n: number): AsyncGenerator<number> {
  for (let i = 0; i < n; i++) yield i;
  throw new Error("failed after " + n);
}

async function* yieldsRejected(): AsyncGenerator<string> {
  yield "first";
  try {
    yield Promise.reject(new Error("rejected value"));
  } catch (e) {
    console.log("in generator:", e instanceof Error ? e.message : "?");
    yield "recovered";
  }
  yield Promise.reject(new TypeError("not caught"));
  yield "never";
}

async function* awaitsRejected(): AsyncGenerator<number> {
  yield 1;
  await Promise.reject(new RangeError("bad await"));
  yield 2;
}

async function* finallyThrows(): AsyncGenerator<number> {
  try {
    yield 1;
    yield 2;
  } finally {
    console.log("cleanup");
    throw new Error("from finally");
  }
}

async function* yieldPromises(): AsyncGenerator<number> {
  yield Promise.resolve(1);
  yield new Promise<number>(res => res(2));
  const v = await Promise.resolve(3);
  yield v;
}

function tick(n: number): void {
  let p = Promise.resolve();
  for (let i = 1; i <= n; i++) p = p.then(() => console.log("tick", i));
}

async function main(): Promise<void> {
  try {
    for await (const x of failing(2)) console.log("got", x);
  } catch (e) {
    console.log("loop caught", e instanceof Error ? e.message : "?");
  }
  const g = failing(1);
  console.log(await g.next());
  try {
    await g.next();
  } catch (e) {
    console.log("next rejected:", e instanceof Error ? e.message : "?");
  }
  console.log(await g.next());

  tick(6);
  const y = yieldsRejected();
  for (let i = 0; i < 5; i++) {
    try {
      const r = await y.next();
      console.log("y", r.value, r.done);
    } catch (e) {
      console.log("y rejected", e instanceof Error ? e.name + ": " + e.message : "?");
    }
  }

  const a = awaitsRejected();
  await a.next().then(r => console.log("a", r.value));
  await a.next().then(r => console.log("a", r.value), (e: unknown) => console.log("a rejected", e instanceof Error ? e.message : "?"));
  await a.next().then(r => console.log("a", r.value, r.done));

  const f = finallyThrows();
  console.log((await f.next()).value);
  try {
    await f.return(undefined);
  } catch (e) {
    console.log("return rejected:", e instanceof Error ? e.message : "?");
  }
  try {
    for await (const x of finallyThrows()) {
      console.log("loop", x);
      break;
    }
  } catch (e) {
    console.log("break rethrows:", e instanceof Error ? e.message : "?");
  }

  const vals: number[] = [];
  for await (const v of yieldPromises()) vals.push(v);
  console.log(vals);
}
main().then(() => console.log("main done"));
