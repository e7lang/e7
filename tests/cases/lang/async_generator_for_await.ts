// @requires-vm: 0.3.1
// for await over async generators and async iterable classes: break, continue, return and throw in the
// body call (and await) the iterator's return(), so its finally blocks run, in Node's order
async function* source(name: string, n: number): AsyncGenerator<number> {
  try {
    for (let i = 1; i <= n; i++) {
      console.log(name, "yields", i);
      yield i;
    }
    console.log(name, "exhausted");
  } finally {
    console.log(name, "finally");
    await null;
    console.log(name, "finally after await");
  }
}

class Countdown {
  private from: number;
  constructor(from: number) { this.from = from; }
  async *[Symbol.asyncIterator](): AsyncGenerator<number> {
    try {
      for (let i = this.from; i > 0; i--) yield i;
    } finally {
      console.log("countdown closed");
    }
  }
}

function tick(n: number): void {
  let p = Promise.resolve();
  for (let i = 1; i <= n; i++) p = p.then(() => console.log("tick", i));
}

async function firstOver(limit: number): Promise<number> {
  for await (const x of source("ret", 10)) {
    if (x > limit) return x * 100;
  }
  return -1;
}

async function main(): Promise<void> {
  tick(10);
  for await (const x of source("brk", 5)) {
    if (x === 2) continue;
    console.log("body", x);
    if (x === 3) break;
  }
  console.log("after break");
  console.log("firstOver", await firstOver(2));
  try {
    for await (const x of source("thr", 5)) {
      if (x === 2) throw new Error("stop at " + x);
    }
  } catch (e) {
    console.log("caught", e instanceof Error ? e.message : "?");
  }
  outer: for await (const a of source("outer", 3)) {
    for await (const b of source("inner", 3)) {
      if (b === 2) continue outer;
      if (a === 3) break outer;
      console.log("pair", a, b);
    }
  }
  for await (const n of new Countdown(3)) {
    console.log("count", n);
    if (n === 2) break;
  }
  let total = 0;
  for await (const n of new Countdown(4)) total += n;
  console.log("total", total);
  // a loop over an exhausted generator does nothing
  const g = source("once", 1);
  for await (const x of g) console.log("first loop", x);
  for await (const x of g) console.log("second loop", x);
  // a let binding per iteration, captured by closures
  const fns: (() => number)[] = [];
  for await (const x of source("cap", 3)) fns.push(() => x);
  console.log(fns.map(f => f()));
}
main().then(() => console.log("done"));
