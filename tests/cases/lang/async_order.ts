async function double(x: number): Promise<number> {
  return x * 2;
}
async function main(): Promise<void> {
  console.log("start");
  const a = await double(21);
  console.log("a", a);
  const b = await Promise.resolve("hi");
  console.log("b", b);
  try {
    await Promise.reject(new Error("bad"));
  } catch (e) {
    if (e instanceof Error) console.log("caught", e.message);
  }
  const all = await Promise.all([double(1), double(2), double(3)]);
  console.log(all);
}
main().then(() => console.log("main done"));
console.log("sync end");

Promise.resolve(1).then(v => console.log("t1", v));
Promise.resolve(2).then(v => { console.log("t2", v); return v + 1; }).then(v => console.log("t3", v));
(async () => { console.log("arrow"); await null; console.log("after await null"); })();
const p = new Promise<string>((resolve, reject) => { console.log("executor"); resolve("done"); });
p.then(s => console.log("p", s)).finally(() => console.log("finally"));
async function fail(): Promise<number> { throw new Error("oops"); }
fail().catch(e => { console.log("fail caught", e.message); return -1; }).then(v => console.log("recovered", v));
async function order(tag: string): Promise<void> {
  console.log(tag, 1);
  await undefined;
  console.log(tag, 2);
  await undefined;
  console.log(tag, 3);
}
order("x");
order("y");
async function retP(): Promise<number> { return double(5); }
retP().then(v => console.log("retP", v));
Promise.allSettled([double(1), fail()]).then(rs => console.log(rs.map(r => r.status), rs[0].value));
Promise.race([double(7), double(8)]).then(v => console.log("race", v));
Promise.any([fail(), double(9)]).then(v => console.log("any", v));
