// the fields of a class instance are own properties also when they hold undefined; a record property
// holding undefined is a missing optional property (SPEC section 9), which JSON.stringify omits in both
interface Rec {
  id: number;
  score?: number;
  tags?: string[];
}
class C {
  a: number = 1;
  b?: number;
  c: string | undefined;
  #d: number = 4;
  d(): number {
    return this.#d;
  }
}
class D extends C {
  e?: boolean;
}
const c = new C();
console.log(Object.keys(c).join(","), JSON.stringify(c), c.d());
console.log(c);
console.log(Object.keys(new D()).join(","), JSON.stringify(new D()));
c.b = 2;
console.log(Object.keys(c).join(","), JSON.stringify(c));
const q: Rec = { id: 1, score: undefined };
const q2: Rec = { ...q, tags: undefined };
console.log(JSON.stringify(q), JSON.stringify(q2));
const r: Rec = { id: 2 };
r.score = 3;
r.tags = ["x"];
console.log(Object.keys(r).join(","), JSON.stringify(r));
