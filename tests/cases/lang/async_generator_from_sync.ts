// @requires-vm: 0.3.1
// yield* of sync iterables in async generators (the async-from-sync iterator of the specification):
// arrays of values and of promises, sync generators with finally blocks, iterable classes; the order of
// the jobs interleaved with other promise chains
function tick(name: string, n: number): void {
  let p = Promise.resolve();
  for (let i = 1; i <= n; i++) p = p.then(() => console.log(name, i));
}

async function* fromArray(): AsyncGenerator<number> {
  yield 0;
  yield* [1, 2, 3];
  yield 4;
}

async function* fromPromises(): AsyncGenerator<string> {
  yield* [Promise.resolve("a"), new Promise<string>(res => res("b")), Promise.resolve("c")];
}

function* numbers(tag: string, n: number): Generator<number> {
  try {
    for (let i = 1; i <= n; i++) {
      console.log(tag, "sync yields", i);
      yield i;
    }
  } finally {
    console.log(tag, "sync finally");
  }
}

async function* fromGen(tag: string): AsyncGenerator<number> {
  try {
    yield* numbers(tag, 3);
    console.log(tag, "after yield*");
  } finally {
    console.log(tag, "async finally");
  }
}

class Trio {
  *[Symbol.iterator](): Generator<string> {
    yield "x";
    yield "y";
    yield "z";
  }
}

async function* fromClass(): AsyncGenerator<string> {
  yield* new Trio();
}

function* summing(): Generator<number, number, number> {
  let total = 0;
  for (let i = 0; i < 3; i++) total += yield i;
  return total;
}

async function* delegating(): AsyncGenerator<number, number, number> {
  const t = yield* summing();
  console.log("yield* gave", t);
  return t * 10;
}

async function main(): Promise<void> {
  tick("t", 40);
  const xs: number[] = [];
  for await (const x of fromArray()) xs.push(x);
  console.log(xs);
  const ss: string[] = [];
  for await (const s of fromPromises()) ss.push(s);
  console.log(ss);
  for await (const n of fromGen("loop")) console.log("got", n);
  for await (const n of fromGen("brk")) {
    console.log("got", n);
    if (n === 2) break;
  }
  const cs: string[] = [];
  for await (const c of fromClass()) cs.push(c);
  console.log(cs.join(""));
  const d = delegating();
  console.log(await d.next(0), await d.next(5), await d.next(6), await d.next(7));
  console.log("end");
}
main();
