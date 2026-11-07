// async generators: generic pipelines, closures, object literal methods, default parameters, return
// inside try / finally, a generator calling its own next() while it runs, optional and integer values
async function* map<T, U>(src: AsyncIterable<T>, f: (x: T) => U): AsyncGenerator<U> {
  for await (const x of src) yield f(x);
}

async function* filter<T>(src: AsyncIterable<T>, keep: (x: T) => boolean): AsyncGenerator<T> {
  for await (const x of src) if (keep(x)) yield x;
}

async function* from<T>(xs: T[]): AsyncGenerator<T> {
  for (const x of xs) yield x;
}

async function collect<T>(src: AsyncIterable<T>): Promise<T[]> {
  const out: T[] = [];
  for await (const x of src) out.push(x);
  return out;
}

function makeCounter(start: number): () => AsyncGenerator<number> {
  let next = start;
  return async function* () {
    while (next < start + 3) yield next++;
  };
}

const source = {
  label: "src",
  async *items(n: number = 2): AsyncGenerator<string> {
    for (let i = 0; i < n; i++) yield this.label + i;
  },
};

async function* withReturn(fail: boolean): AsyncGenerator<string, string> {
  try {
    yield "in try";
    if (fail) throw new Error("fail");
    return "returned";
  } finally {
    console.log("finally runs");
    await null;
    console.log("finally after await");
  }
}

let self: AsyncGenerator<number> | null = null;
async function* selfCalling(): AsyncGenerator<number> {
  yield 1;
  if (self) {
    // a request made while the generator runs waits in its queue
    self.next().then(r => console.log("own request", r.value, r.done));
  }
  yield 2;
  yield 3;
}

async function* maybe(): AsyncGenerator<number | undefined> {
  yield 1;
  yield undefined;
  yield 3;
}

async function* ints(): AsyncGenerator<number> {
  const xs = new Int32Array([7, -8, 9]);
  for (const x of xs) yield x;
}

async function* nested(): AsyncGenerator<string> {
  for await (const a of from(["x", "y"])) {
    for await (const b of from([1, 2])) yield a + b;
  }
}

async function main(): Promise<void> {
  const doubled = map(from([1, 2, 3, 4, 5]), x => x * 2);
  const big = filter(doubled, x => x > 4);
  console.log(await collect(map(big, x => "#" + x)));
  const c = makeCounter(10);
  console.log(await collect(c()), await collect(c()));
  console.log(await collect(source.items()), await collect(source.items(3)));
  const w = withReturn(false);
  console.log(await w.next(), await w.next(), await w.next());
  const f = withReturn(true);
  console.log(await f.next());
  await f.next().then(r => console.log(r), (e: unknown) => console.log("rejected", e instanceof Error ? e.message : "?"));
  self = selfCalling();
  for (let i = 0; i < 4; i++) {
    const r = await self.next();
    console.log("main request", r.value, r.done);
  }
  for await (const m of maybe()) console.log(m);
  const mm = maybe();
  const r1 = await mm.next();
  console.log(r1, r1.value === undefined);
  console.log(await collect(ints()));
  console.log(await collect(nested()));
}
main();
