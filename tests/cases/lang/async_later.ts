import { later, laterReject } from "e7:test";
async function step(name: string, n: number): Promise<number> {
  const v = await later(n);
  console.log(name, "got", v);
  const w = await later(v * 10);
  console.log(name, "got", w);
  return w + 1;
}
async function main(): Promise<void> {
  const [a, b] = await Promise.all([step("a", 1), step("b", 2)]);
  console.log("sum", a + b);
  try {
    await laterReject(new Error("nope"));
  } catch (e) {
    if (e instanceof Error) console.log("caught", e.message);
  }
  later("x").then(v => console.log("then", v));
  Promise.resolve(0).then(() => console.log("micro first"));
  await later(true);
  console.log("end");
}
main();
console.log("sync done");
