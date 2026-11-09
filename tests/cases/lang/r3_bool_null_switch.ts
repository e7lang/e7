// third test round of 0.1.3 (bool_null_switch): must print what Node.js prints
function b2(b: boolean | null): string { switch (b) { case true: return "t"; case false: return "f"; case null: return "n"; } }
type Dir = "n" | "s";
function d2(d: Dir | undefined): string { switch (d) { case "n": return "N"; case "s": return "S"; case undefined: return "U"; } }
enum E { A, B }
function e2(e: E | null): string { switch (e) { case E.A: return "A"; case E.B: return "B"; case null: return "null"; } }
console.log(b2(true), b2(false), b2(null), d2(undefined), d2("s"), e2(null), e2(E.B));
