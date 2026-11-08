// An argument that may be undefined for a parameter with a constant default: the default applies.
enum Lv {
  Low = 1,
  High = 2,
}
function f(p: number = 1, s: string = "d", l: Lv = Lv.High, n: number = -2.5): string {
  return `${p}${s}${l}${n}`;
}
function g(p?: number, s?: string, l?: Lv): string {
  return f(p, s, l) + "|" + f(p, s, l, p);
}
class C {
  constructor(readonly k: number = 7) {}
  static st(x: boolean = true): boolean {
    return x;
  }
}
const ou: number | undefined = undefined;
console.log(g(), g(5, "x", Lv.Low), f(undefined, undefined), new C(ou).k, new C(3).k, C.st(undefined));
