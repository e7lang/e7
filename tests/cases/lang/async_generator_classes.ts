// @requires-vm: 0.3.1
// async generator methods in class hierarchies (overrides through the vtable), generic classes, a loop
// whose body throws while the generator's return() throws too, return from a for await inside an async
// generator, many queued requests, a deep chain of yield*
abstract class Source {
  abstract name(): string;
  async *items(): AsyncGenerator<string> {
    yield this.name() + ":base";
  }
  async *[Symbol.asyncIterator](): AsyncGenerator<string> {
    yield* this.items();
    yield this.name() + ":end";
  }
}

class Letters extends Source {
  name(): string { return "letters"; }
  async *items(): AsyncGenerator<string> {
    for (const c of "abc") {
      await null;
      yield c;
    }
  }
}

class Empty extends Source {
  name(): string { return "empty"; }
}

class Queue<T> {
  private xs: T[] = [];
  push(x: T): void { this.xs.push(x); }
  async *drain(): AsyncGenerator<T> {
    while (this.xs.length > 0) {
      const x = this.xs.shift();
      if (x !== undefined) yield x;
    }
  }
}

async function* badCleanup(): AsyncGenerator<number> {
  try {
    yield 1;
    yield 2;
  } finally {
    throw new Error("cleanup failed");
  }
}

async function* firstOfEach(sources: Source[]): AsyncGenerator<string> {
  for (const s of sources) {
    for await (const x of s) {
      yield x;
      break;
    }
  }
}

async function* untilBig(): AsyncGenerator<number, number> {
  let n = 0;
  for await (const x of countUp()) {
    if (x > 3) return x * 10;
    yield x;
    n++;
  }
  return n;
}

async function* countUp(): AsyncGenerator<number> {
  try {
    for (let i = 0; ; i++) yield i;
  } finally {
    console.log("countUp closed");
  }
}

async function* chain(depth: number): AsyncGenerator<number> {
  if (depth > 0) yield* chain(depth - 1);
  yield depth;
}

async function main(): Promise<void> {
  for (const s of [new Letters(), new Empty()]) {
    const out: string[] = [];
    for await (const x of s) out.push(x);
    console.log(out.join(" "));
  }
  console.log("firsts");
  for await (const x of firstOfEach([new Letters(), new Empty()])) console.log(" ", x);

  const q = new Queue<number>();
  q.push(1);
  q.push(2);
  const d = q.drain();
  console.log((await d.next()).value);
  q.push(3);
  const rest: number[] = [];
  for await (const x of d) rest.push(x);
  console.log(rest);

  try {
    for await (const x of badCleanup()) {
      console.log("body", x);
      throw new Error("body failed");
    }
  } catch (e) {
    console.log("caught", e instanceof Error ? e.message : "?");
  }
  try {
    for await (const x of badCleanup()) {
      console.log("body", x);
      break;
    }
  } catch (e) {
    console.log("caught", e instanceof Error ? e.message : "?");
  }

  const u = untilBig();
  const results: string[] = [];
  for (let i = 0; i < 5; i++) {
    const r = await u.next();
    results.push(`${r.value}/${r.done}`);
  }
  console.log(results.join(" "));

  // many requests queued at once
  const g = countUp();
  const ps: Promise<IteratorResult<number>>[] = [];
  for (let i = 0; i < 2000; i++) ps.push(g.next());
  ps.push(g.return(undefined));
  ps.push(g.next());
  const rs = await Promise.all(ps);
  let sum = 0;
  for (const r of rs) if (!r.done && r.value !== undefined) sum += r.value;
  console.log(sum, rs[1999].value, rs[2000].done, rs[2001].done);

  let last = -1;
  let count = 0;
  for await (const x of chain(60)) {
    last = x;
    count++;
  }
  console.log(count, last);
}
main();
