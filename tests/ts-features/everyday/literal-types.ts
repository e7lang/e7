// @feature: Literal types
// @category: Everyday Types
// @status: supported
// @note: string, numeric and boolean literal types and unions of literals of one base type, checked as tsc checks them and narrowed by `===` and `switch`; at run time they are their base type
type Alignment = "left" | "right" | "center";
type Dice = 1 | 2 | 3 | 4 | 5 | 6;
function pad(s: string, align: Alignment): string {
  if (align === "left") return s.padEnd(7, ".");
  if (align === "right") return s.padStart(7, ".");
  const c: "center" = align;
  return c + ":" + s;
}
const a: Alignment = "right";
const roll: Dice = 4;
const yes: true = true;
console.log(pad("ab", "left"), pad("ab", a), pad("ab", "center"), roll + 1, yes);
