// return() and throw() in every state of an async generator: not started, suspended at a yield,
// running, awaiting, awaiting a return value, completed; finally blocks around yields
async function* gen(label: string): AsyncGenerator<string, string> {
  console.log(label, "body starts");
  try {
    yield label + "-1";
    await null;
    yield label + "-2";
    return label + "-ret";
  } catch (e) {
    console.log(label, "caught", e instanceof Error ? e.message : "?");
    yield label + "-from-catch";
    return label + "-after-catch";
  } finally {
    console.log(label, "finally");
  }
}

function show(name: string, p: Promise<IteratorResult<string, string>>): Promise<void> {
  return p.then(r => console.log(name, r.value, r.done), (e: unknown) => console.log(name, "rejected", e instanceof Error ? e.message : "?"));
}

function tick(name: string, n: number): void {
  let p = Promise.resolve();
  for (let i = 1; i <= n; i++) p = p.then(() => console.log(name, i));
}

async function main(): Promise<void> {
  // not started: return(v) completes it without running the body (after awaiting v)
  const a = gen("a");
  tick("ta", 4);
  await show("a.return", a.return("r"));
  await show("a.next", a.next());
  // not started: throw(e) completes it and rejects
  const b = gen("b");
  await show("b.throw", b.throw(new Error("boom")));
  await show("b.next", b.next());
  // suspended at a yield: return runs the finally block
  const c = gen("c");
  await show("c.next", c.next());
  tick("tc", 4);
  await show("c.return", c.return("stop"));
  await show("c.next", c.next());
  // suspended at a yield: throw is caught by the body, which yields again
  const d = gen("d");
  await show("d.next", d.next());
  await show("d.throw", d.throw(new Error("oops")));
  await show("d.next", d.next());
  await show("d.next", d.next());
  // a second throw while in the catch block: not caught, the generator ends
  const e = gen("e");
  await show("e.next", e.next());
  await show("e.throw1", e.throw(new Error("one")));
  await show("e.throw2", e.throw(new Error("two")));
  await show("e.next", e.next());
  // completed: return and throw
  const f = gen("f");
  for await (const x of f) console.log("f got", x);
  await show("f.return", f.return("late"));
  await show("f.throw", f.throw(new Error("late")));
  await show("f.next", f.next());
  // awaiting (inside the body): requests wait in the queue
  const g = gen("g");
  await show("g.next", g.next());
  const p1 = show("g.next2", g.next());   // the body is now at `await null`
  const p2 = show("g.return", g.return("while-awaiting"));
  const p3 = show("g.throw", g.throw(new Error("queued")));
  tick("tg", 6);
  await Promise.all([p1, p2, p3]);
  // awaiting a return value: more requests queue behind it
  const h = gen("h");
  const r1 = show("h.return", h.return("x"));
  const r2 = show("h.next", h.next());
  const r3 = show("h.return2", h.return("y"));
  const r4 = show("h.throw", h.throw(new Error("z")));
  tick("th", 6);
  await Promise.all([r1, r2, r3, r4]);
  console.log("end");
}
main();
