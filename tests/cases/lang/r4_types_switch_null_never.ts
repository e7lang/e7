// fourth test round of 0.1.3 (types/switch_null_never): must print what Node.js prints
type D = "a" | "b";
function nv(x: never): string { return "never"; }
function f(d: D | null): string {
  switch (d) { case "a": return "A"; case "b": return "B"; case null: return "N"; default: return nv(d); }
}
console.log(f(null), f("a"));
