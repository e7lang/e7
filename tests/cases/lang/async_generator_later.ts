// @requires-vm: 0.3.1
// async generators driven by promises settled in later turns (e7:test later / laterReject: host tickets)
import { later, laterReject } from "e7:test";

async function* pages(n: number): AsyncGenerator<string[]> {
  for (let i = 0; i < n; i++) {
    const items = await later(["item" + (2 * i), "item" + (2 * i + 1)]);
    console.log("page", i, "loaded");
    yield items;
  }
}

async function* ticker(): AsyncGenerator<number, void, boolean> {
  let n = 0;
  for (;;) {
    const stop = yield await later(n++);
    if (stop) return;
  }
}

async function* unreliable(): AsyncGenerator<number> {
  yield await later(1);
  await laterReject(new Error("network down"));
  yield 2;
  yield 3;
}

async function main(): Promise<void> {
  const all: string[] = [];
  for await (const page of pages(3)) {
    console.log("got page", page.length);
    all.push(...page);
  }
  console.log(all.join(" "));

  const t = ticker();
  console.log((await t.next(false)).value);
  console.log((await t.next(false)).value);
  console.log((await t.next(false)).value);
  console.log((await t.next(true)).done);

  // two consumers of one generator: requests interleave with the later() turns
  const p = pages(2);
  const a = p.next();
  const b = p.next();
  later("between").then(v => console.log(v));
  console.log((await a).value, (await b).value);

  try {
    for await (const x of unreliable()) console.log("value", x);
  } catch (e) {
    console.log("failed:", e instanceof Error ? e.message : "?");
  }
}
main().then(() => console.log("done"));
console.log("sync end");
