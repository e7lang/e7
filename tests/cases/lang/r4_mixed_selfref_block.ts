// fourth test round of 0.1.3 (mixed/selfref_block): must print what Node.js prints
function on(h: () => void): () => boolean { return () => { h(); return true; }; }
const topA: () => boolean = on(() => console.log("topA handler"));
const topB: () => boolean = on(() => console.log("topB sees", typeof topB));
function inFn(): boolean {
  const off: () => boolean = on(() => { console.log("fn handler"); });
  const off2: () => boolean = on(() => { console.log("fn handler2", off2 !== null); });
  return off() && off2();
}
console.log(topA(), topB(), inFn());
