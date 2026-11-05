// @feature: Loose equality `==` / `!=`
// @category: JavaScript: Syntax
// @status: partial
// @note: only `== null` / `!= null` (null or undefined)
function describe(x: string | null | undefined): string {
  return x == null ? "missing" : x;
}
console.log(describe(null), describe(undefined), describe("v"), "" != null);
