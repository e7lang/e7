// An exported async entry function (called by the host: `e7` calls main and prints its result) that
// consumes async generators; the expected output (.out) is Node's with `main().then(console.log)`.
import { later } from "e7:test";

async function* fetchAll(ids: number[]): AsyncGenerator<number> {
  for (const id of ids) {
    const v = await later(id * 10);
    console.log("fetched", id);
    yield v;
  }
}

async function* take(src: AsyncIterable<number>, n: number): AsyncGenerator<number> {
  if (n <= 0) return;
  let k = 0;
  for await (const v of src) {
    yield v;
    if (++k >= n) return;
  }
}

export async function main(): Promise<number> {
  let sum = 0;
  for await (const v of take(fetchAll([1, 2, 3, 4, 5]), 3)) {
    console.log("value", v);
    sum += v;
  }
  return sum;
}
console.log("module loaded");
