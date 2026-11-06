// @feature: Tagged templates and `String.raw`
// @category: JavaScript: Syntax
// @status: supported
// @note: the tag gets the parts as a `readonly string[]` (TypeScript's `TemplateStringsArray`; its `raw` property is not available) and the values
function upper(strings: TemplateStringsArray, ...values: string[]): string {
  return strings.map((s, i) => s + (values.at(i) ?? "").toUpperCase()).join("");
}
console.log(upper`hello ${"world"}!`, String.raw`a\nb`);
