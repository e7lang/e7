// @feature: Async iteration (`for await`, async generators)
// @category: Iterators and Generators
// @status: supported
// @note: async generators need e7vm 0.3.1; `next()` on a finished generator whose return type is a primitive gives NaN / 0 instead of undefined (as for generators)
async function sum(xs: Promise<number>[]): Promise<number> {
  let t = 0;
  for await (const x of xs) t += x;
  return t;
}

async function* numbers(n: number): AsyncGenerator<number> {
  for (let i = 1; i <= n; i++) {
    await null;
    yield i;
  }
}

async function* squares(src: AsyncIterable<number>): AsyncGenerator<number> {
  for await (const x of src) yield x * x;
}

class Feed {
  private items = ["a", "b", "c"];
  async *[Symbol.asyncIterator](): AsyncGenerator<string> {
    try {
      for (const it of this.items) yield it;
    } finally {
      console.log("feed closed");
    }
  }
}

function* evens(): Generator<number> {
  yield 10;
  yield 20;
}

async function* both(): AsyncGenerator<number> {
  yield* numbers(2);
  yield* squares(numbers(3));
  yield* [Promise.resolve(7), Promise.resolve(8)];
  yield* evens();
}

async function main(): Promise<void> {
  console.log(await sum([Promise.resolve(1), Promise.resolve(2)]));
  for await (const s of squares(numbers(3))) console.log("square", s);
  for await (const item of new Feed()) {
    console.log("item", item);
    if (item === "b") break;
  }
  const g = both();
  const first = g.next();
  const second = g.next();
  console.log(await first, await second);
  console.log(await g.return(undefined), await g.next());
  const all: number[] = [];
  for await (const n of both()) all.push(n);
  console.log(all);
}
main();
