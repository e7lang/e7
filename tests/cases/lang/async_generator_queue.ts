// several next() calls without awaiting are queued and settled in order, interleaved with other jobs
async function* counter(label: string): AsyncGenerator<string, string, number> {
  console.log(label, "start");
  let total = 0;
  for (let i = 0; i < 3; i++) {
    const got = yield label + i;
    console.log(label, "received", got);
    total += got;
    await null;
    console.log(label, "after await", i);
  }
  return "total " + total;
}

function tick(name: string, n: number): void {
  let p = Promise.resolve();
  for (let i = 1; i <= n; i++) p = p.then(() => console.log(name, i));
}

async function main(): Promise<void> {
  const g = counter("a");
  const p1 = g.next(100);
  const p2 = g.next(1);
  const p3 = g.next(2);
  const p4 = g.next(3);
  const p5 = g.next(4);
  console.log("queued");
  tick("tick", 12);
  p1.then(r => console.log("p1", r.value, r.done));
  p2.then(r => console.log("p2", r.value, r.done));
  p3.then(r => console.log("p3", r.value, r.done));
  p4.then(r => console.log("p4", r.value, r.done));
  p5.then(r => console.log("p5", r.value, r.done));
  const all = await Promise.all([p1, p2, p3, p4, p5]);
  console.log(all.length, all[3], all[4]);

  // return / throw queued behind next calls
  const h = counter("b");
  const q1 = h.next(0);
  const q2 = h.return("early");
  const q3 = h.next(5);
  const q4 = h.throw(new Error("late"));
  const q5 = h.next(6);
  q1.then(r => console.log("q1", r));
  q2.then(r => console.log("q2", r));
  q3.then(r => console.log("q3", r));
  q4.then(r => console.log("q4", r), (e: unknown) => console.log("q4 rejected", e instanceof Error ? e.message : ""));
  q5.then(r => console.log("q5", r));
  tick("tock", 8);
  await q5;
  console.log("after q5");

  // next() calls queued while the body awaits
  const k = counter("c");
  const first = k.next(0);
  await first;
  const s1 = k.next(10);
  const s2 = k.next(20);
  tick("tuck", 6);
  console.log((await s1).value, (await s2).value);
  const rest = await Promise.all([k.next(30), k.next(40), k.next(50)]);
  for (const r of rest) console.log(r.value, r.done);
  console.log("end");
}
main();
