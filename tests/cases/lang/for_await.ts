async function sum(xs: Promise<number>[]): Promise<number> {
  let t = 0;
  for await (const x of xs) { t += x; console.log("got", x); }
  return t;
}
async function names(): Promise<void> {
  for await (const s of ["a", "b"]) console.log("name", s);
}
sum([Promise.resolve(1), Promise.resolve(2)]).then(t => console.log("sum", t));
names();
Promise.resolve().then(() => console.log("tick1")).then(() => console.log("tick2")).then(() => console.log("tick3")).then(() => console.log("tick4"));
