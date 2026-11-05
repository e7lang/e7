// @feature: Literal types
// @category: Everyday Types
// @status: partial
// @note: string literal types and unions of them are checked by tsc and treated as `string` by E7
type Alignment = "left" | "right" | "center";
function pad(s: string, align: Alignment): string {
  if (align === "left") return s.padEnd(7, ".");
  if (align === "right") return s.padStart(7, ".");
  return s;
}
const a: Alignment = "right";
console.log(pad("ab", "left"), pad("ab", a), pad("ab", "center"));
