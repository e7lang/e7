// @requires-vm: 0.3.1
// console.log names the kind of generator and async functions as Node.js does
function* g(): Generator<number, void, unknown> { yield 1; }
async function a(): Promise<number> { return 1; }
async function* ag(): AsyncGenerator<number, void, unknown> { yield 1; }
const o = { *m(): Generator<number, void, unknown> { yield 1; }, async n(): Promise<number> { return 1; } };
console.log(g, a, ag, o);
const anon = [function* (): Generator<number, void, unknown> { yield 1; }];
const anon2 = [async (): Promise<number> => 1];
console.log(anon, anon2, [async function* (): AsyncGenerator<number, void, unknown> { yield 1; }]);
const box2 = {
  items: [1, 2, 3],
  get total(): number { return this.items.reduce((a, b) => a + b, 0); },
  set first(v: number) { this.items[0] = v; },
  async *each(): AsyncGenerator<number, void, unknown> {
    for (const x of this.items) { await null; yield x * 10; }
  },
};
async function main2(): Promise<void> {
  box2.first = 5;
  const seen: number[] = [];
  for await (const v of box2.each()) seen.push(v);
  console.log(box2.total, seen.join(","), JSON.stringify(box2), Object.keys(box2).join("|"));
  console.log(box2);
}
await main2();
