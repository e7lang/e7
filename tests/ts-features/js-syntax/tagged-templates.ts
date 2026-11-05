// @feature: Tagged templates and `String.raw`
// @category: JavaScript: Syntax
// @status: unsupported
// @why: not implemented yet
// @instead: a function taking the parts as arguments
// @error: tagged templates are not supported
function upper(strings: TemplateStringsArray, ...values: string[]): string {
  return strings.map((s, i) => s + (values[i] ?? "").toUpperCase()).join("");
}
console.log(upper`hello ${"world"}!`, String.raw`a\nb`);
