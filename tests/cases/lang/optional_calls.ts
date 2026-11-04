class Holder {
  val = 21;
  onDone?: (n: number) => void;
  compute?: () => number;
  get2(): number { return this.val * 2; }
  finish(): void { this.onDone?.(this.val); }
}
const h = new Holder();
console.log(h.get2?.(), h.compute?.());
h.finish();
h.onDone = (n) => console.log("done", n);
h.compute = () => 99;
h.finish();
console.log(h.compute?.());
interface Opts { log?: (s: string) => void; name: string }
const o: Opts = { name: "x" };
o.log?.("not printed");
const o2: Opts = { name: "y", log: s => console.log("log:", s) };
o2.log?.("printed");
let fn: ((x: number) => number) | null = Math.random() > 2 ? (x => x) : null;
console.log(fn?.(1));
fn = x => x * 3;
console.log(fn?.(2));
const maybe: Holder | null = Math.random() > 2 ? h : null;
console.log(maybe?.get2(), maybe?.val);
