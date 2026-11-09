// third test round of 0.1.3 (literal_nullish): must print what Node.js prints
type Filter = "all" | "active" | "done";
interface S { filter: Filter; n: number }
interface A { filter?: Filter }
function f(s: S, a: A): S {
  return { ...s, filter: a.filter ?? "all" };
}
console.log(f({ filter: "done", n: 1 }, {}), f({ filter: "done", n: 1 }, { filter: "active" }));
const a: A = {};
const g: Filter = a.filter ?? "all";
const s2: S = { filter: a.filter ?? "done", n: 2 };
console.log(g, s2);
