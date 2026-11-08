// switch on a nullable value: case null / undefined, and the other cases see the value narrowed.
type Op = "eq" | "neq" | "gt";
enum Lv {
  Low = 1,
  High = 2,
}
function a(op: string | undefined): string {
  switch (op) {
    case "eq": return "EQ" + op.length;
    case undefined: return "none";
    default: return "other";
  }
}
function b(op?: Op): string {
  switch (op) {
    case "eq":
    case "neq": {
      const k: "eq" | "neq" = op;
      return k;
    }
    default: return "def:" + String(op);
  }
}
function c(n: number | null): number {
  switch (n) {
    case 1: return 10;
    case 2: return n * 100;
    case null: return -1;
  }
  return 0;
}
function d(l: Lv | undefined, f: boolean | null): string {
  let r = "";
  switch (l) {
    case Lv.Low: r += "L"; break;
    case Lv.High: r += "H"; break;
    default: r += "?";
  }
  switch (f) {
    case true: r += "T"; break;
    case false: r += "F"; break;
    case null: r += "N";
  }
  return r;
}
console.log(a("eq"), a(undefined), a("x"), b("neq"), b(), b("gt"));
console.log(c(1), c(2), c(null), c(3), d(Lv.High, true), d(undefined, null), d(Lv.Low, false));
