// a yield in a finally block turns return(v) back into a yield; the requests queued behind it wait
async function* g(): AsyncGenerator<string, string> {
  try {
    yield "a";
    yield "b";
  } finally {
    yield "from finally";
    console.log("finally resumed");
    await null;
  }
  return "end";
}
function tick(n: number): void {
  let p = Promise.resolve();
  for (let i = 1; i <= n; i++) p = p.then(() => console.log("tick", i));
}
async function main(): Promise<void> {
  const it = g();
  console.log(await it.next());
  tick(8);
  const p1 = it.return("x");
  const p2 = it.next();
  const p3 = it.throw(new Error("t"));
  console.log(await p1);
  console.log(await p2);
  try { console.log(await p3); } catch (e) { console.log("p3 rejected", e instanceof Error ? e.message : ""); }
  const j = g();
  await j.next();
  console.log(await j.throw(new Error("in")));
  try { console.log(await j.next()); } catch (e) { console.log("j rejected", e instanceof Error ? e.message : ""); }
}
main();
