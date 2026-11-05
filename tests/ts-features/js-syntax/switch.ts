// @feature: `switch` with fall-through
// @category: JavaScript: Syntax
// @status: supported
function grade(score: number): string {
  let out = "";
  switch (Math.floor(score / 10)) {
    case 10:
    case 9: out += "A"; break;
    case 8: out += "B";
    case 7: out += "C"; break;
    default: out += "F";
  }
  return out;
}
function day(s: string): number {
  switch (s) { case "mon": return 1; case "tue": return 2; default: return 0; }
}
console.log(grade(95), grade(85), grade(72), grade(10), day("tue"));
