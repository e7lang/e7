// Parameter properties whose type includes undefined keep the value passed to the constructor.
class C {
  constructor(readonly reason?: string, public k?: number, readonly u: string | undefined = undefined, readonly d: string = "d") {}
}
const p = new C("because", 5, "u");
console.log(p.reason, p.k, p.u, p.d);
const q = new C();
console.log(q.reason, q.k, q.u, q.d);
class D extends C {
  constructor(readonly extra?: boolean) {
    super("base");
  }
}
const d = new D(true);
console.log(d.reason, d.extra, new D().extra);
