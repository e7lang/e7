// object spread and rest copy the source's properties in their run-time order
interface Rec {
  id: number;
  score?: number;
  tags: string[];
}
const r: Rec = { id: 1, tags: [] };
r.score = 2;
console.log(JSON.stringify({ ...r }), JSON.stringify(r));
console.log(Object.keys({ ...r }).join(","), Object.keys({ ...r, id: 5 }).join(","));
const r2: Rec = { tags: ["t"], id: 2, score: 3 };
console.log(JSON.stringify({ ...r2 }), JSON.stringify({ ...r2, extra: true }));
const copy: Rec = { ...r2 };
console.log(JSON.stringify(copy), JSON.stringify({ ...copy }));
const r3: Rec = { id: 3, score: 4, tags: [] };
const list: Rec[] = [r, r2, r3];
for (const x of list) console.log(JSON.stringify({ ...x }), JSON.stringify({ first: 0, ...x, id: -1 }));
function clone(x: Rec): Rec {
  return { ...x };
}
console.log(list.map((x) => JSON.stringify(clone(x))).join(" "));
const twice = { ...{ ...r } };
console.log(JSON.stringify(twice));
interface P {
  x: number;
  y: number;
}
class Pt {
  y: number = 1;
  x: number = 2;
}
const ps: P[] = [{ x: 1, y: 2 }, { y: 3, x: 4 }, new Pt()];
for (const p of ps) console.log(JSON.stringify({ ...p }), Object.keys({ ...p, z: 0 }).join(""));
const merged = Object.assign({ a: 1 }, r2);
console.log(JSON.stringify(merged));
console.log({ ...r2 });

// object rest
const r4: Rec = { tags: ["a"], id: 4 };
r4.score = 5;
const { id, ...rest } = r4;
console.log(id, JSON.stringify(rest), Object.keys(rest).join(","));
for (const { tags, ...others } of list) console.log(tags.length, JSON.stringify(others));
function f({ x, ...more }: P): string {
  return x + JSON.stringify(more);
}
console.log(ps.map((p) => f(p)).join(" "));
