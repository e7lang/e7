// optional methods in interfaces and optional function-typed properties and fields: called with ?.()
// or after a check that narrows them to non-null
interface Plugin {
  name: string;
  init?(): string;
  transform?(s: string, times: number): string;
  onDone?: (n: number) => void;
}
function run(p: Plugin): string {
  let out = p.name + ":" + (p.init?.() ?? "no-init");
  if (p.transform) out += ":" + p.transform("ab", 2);
  if (p.onDone !== undefined) p.onDone(out.length);
  return out;
}
const plain: Plugin = { name: "plain" };
const full: Plugin = {
  name: "full",
  init: () => "ready",
  transform(s: string, times: number): string { return s.repeat(times); },
  onDone: (n) => console.log("done", n),
};
console.log(run(plain), run(full));

class Task {
  callback?: (n: number) => number;
  run(x: number): number {
    const viaOptionalCall = this.callback?.(x + 100);
    if (viaOptionalCall !== undefined) console.log("optional call", viaOptionalCall);
    if (this.callback) return this.callback(x);
    return -1;
  }
}
const t = new Task();
console.log(t.run(1));
t.callback = (n) => n * 10;
console.log(t.run(2));
if (t.callback) console.log(t.callback(3));
