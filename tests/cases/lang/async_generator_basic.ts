// async function*: declarations, expressions, methods, inferred types, the IteratorResult records
async function* range(from: number, to: number): AsyncGenerator<number> {
  for (let i = from; i < to; i++) yield i;
}

// the return type inferred: AsyncGenerator<string, void>
async function* words(text: string) {
  for (const w of text.split(" ")) {
    await null;
    yield w.toUpperCase();
  }
}

const letters = async function* (): AsyncGenerator<string, string> {
  yield "a";
  yield "b";
  return "z";
};

class Store {
  private items: string[] = [];
  add(s: string): this { this.items.push(s); return this; }
  async *entries(prefix: string): AsyncGenerator<string> {
    for (const it of this.items) yield prefix + it;
  }
  async *[Symbol.asyncIterator](): AsyncIterator<string> {
    for (const it of this.items) yield it;
  }
}

interface Point { x: number; y: number }
async function* points(): AsyncIterableIterator<Point> {
  yield { x: 1, y: 2 };
  yield { x: 3, y: 4 };
}

async function* flags(): AsyncGenerator<boolean, boolean> {
  yield true;
  yield false;
  return true;
}

async function* bigs(): AsyncGenerator<bigint> {
  yield 1n;
  yield 2n ** 70n;
}

async function main(): Promise<void> {
  let sum = 0;
  for await (const i of range(1, 5)) sum += i;
  console.log("sum", sum);
  const ws: string[] = [];
  for await (const w of words("hello async world")) ws.push(w);
  console.log(ws.join(","));
  const l = letters();
  console.log(await l.next(), await l.next(), await l.next(), await l.next());
  const s = new Store().add("x").add("y");
  for await (const e of s.entries("#")) console.log("entry", e);
  for await (const e of s) console.log("item", e);
  for await (const { x, y } of points()) console.log("point", x, y);
  const f = flags();
  console.log(await f.next(), await f.next(), await f.next());
  for await (const b of bigs()) console.log(b);
  const g = range(0, 1);
  console.log(g, String(g), typeof g);
  const r = await g.next();
  console.log(r.value, r.done, r);
  const it: AsyncIterator<number> = range(10, 12);
  const a = await it.next();
  const b = await it.next();
  const c = await it.next();
  console.log(a.value, b.value, c.value, c.done);
  const iterable: AsyncIterable<number> = range(7, 9);
  for await (const n of iterable) console.log("iterable", n);
}
main();
