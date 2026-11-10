// Promise<void> settled with an explicit undefined, and typed arrays made from an empty array literal.
import { later } from "e7:test";
const a = new Uint8Array([]);
const b = new Float64Array([]);
const c = new BigInt64Array([]);
const d = new Int16Array([1, -2]);
console.log(a.length, b.length, c.length, d[1]);
async function main(): Promise<void> {
  await Promise.resolve<void>(undefined);
  await later<void>(undefined);
  const p: Promise<void> = Promise.resolve(undefined);
  await p;
  console.log("done");
}
main();
