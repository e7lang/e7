// yield* edge cases: an inner generator that yields from its finally block when returned, a throw the
// inner generator does not catch, a return value that is a rejected promise, queued requests
async function* stubborn(): AsyncGenerator<string> {
  try {
    yield "s1";
    yield "s2";
  } finally {
    yield "from-finally";
    console.log("stubborn finally done");
  }
}

async function* wrap(): AsyncGenerator<string> {
  try {
    yield* stubborn();
    yield "after";
  } finally {
    console.log("wrap finally");
  }
}

async function* plain(): AsyncGenerator<number> {
  yield 1;
  yield 2;
}

async function* wrapPlain(): AsyncGenerator<number> {
  try {
    yield* plain();
  } catch (e) {
    console.log("wrapPlain caught", e instanceof Error ? e.message : "?");
    yield 99;
  }
}

function tick(name: string, n: number): void {
  let p = Promise.resolve();
  for (let i = 1; i <= n; i++) p = p.then(() => console.log(name, i));
}

async function main(): Promise<void> {
  const w = wrap();
  console.log(await w.next());
  tick("t", 10);
  console.log(await w.return(undefined));
  console.log(await w.next());
  console.log(await w.next());

  const p = wrapPlain();
  console.log(await p.next());
  tick("u", 10);
  console.log(await p.throw(new Error("into plain")));
  console.log(await p.next());

  // requests queued while the delegation runs
  const q = wrap();
  const r1 = q.next();
  const r2 = q.next();
  const r3 = q.return(undefined);
  const r4 = q.next();
  tick("v", 12);
  for (const r of await Promise.all([r1, r2, r3, r4])) console.log(r);
  console.log(await q.next());
}
main().then(() => console.log("done"));
