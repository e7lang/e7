// @feature: Control-flow analysis (early return, `throw`, loops)
// @category: Narrowing
// @status: supported
function padLeft(padding: number | null, input: string): string {
  if (padding === null) return input;
  return " ".repeat(padding) + input;
}
function first(xs: string[]): string {
  let found: string | null = null;
  for (const x of xs) {
    if (x.startsWith("b")) { found = x; break; }
  }
  if (found === null) throw new Error("none");
  return found.toUpperCase();
}
console.log(`[${padLeft(3, "x")}]`, `[${padLeft(null, "y")}]`, first(["a", "bc"]));
