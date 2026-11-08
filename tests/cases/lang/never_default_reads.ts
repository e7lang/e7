// The default clause of an exhaustive switch reads the value as it is (a property path or a variable
// narrowed to never), e.g. a number from the wire cast to an enum that is not one of its members.
type Kind = "a" | "b";
interface R {
  kind: Kind;
  inner: { k: Kind };
}
enum MsgType {
  HeartRate = 0x10,
  Battery = 0x11,
}
function f(r: R): string {
  switch (r.kind) {
    case "a": return "x";
    case "b": return "y";
    default: return "unknown " + r.kind;
  }
}
function g(r: R): string {
  switch (r.inner.k) {
    case "a": return "x";
    case "b": return "y";
    default: {
      const n: never = r.inner.k;
      return `bad ${r.inner.k}` + String(n);
    }
  }
}
function name(t: MsgType): string {
  switch (t) {
    case MsgType.HeartRate: return "hr";
    case MsgType.Battery: return "bat";
    default: return "unknown 0x" + (t as number).toString(16) + " " + t;
  }
}
const bytes = Uint8Array.of(0x11, 0x99);
const a = bytes[0] as MsgType;
const num: number = bytes[1];
const b: MsgType = num;
const c: MsgType = 0x10;
console.log(f({ kind: "a", inner: { k: "b" } }), g({ kind: "b", inner: { k: "b" } }));
console.log(a === MsgType.Battery, name(a), name(b), name(c), `${b}`);
