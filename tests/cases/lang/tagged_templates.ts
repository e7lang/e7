function tag(strings: TemplateStringsArray, ...values: number[]): string {
  let out = "";
  strings.forEach((s, i) => { out += s + (i < values.length ? `[${values[i] * 2}]` : ""); });
  return out;
}
const a = 3, b = 4;
console.log(tag`x=${a}, y=${b}!`, tag`plain`, String.raw`C:\new\table ${a + b}\n`);
function upper(parts: readonly string[], ...names: string[]): string {
  return parts.map((p, i) => p + (names.at(i) ?? "").toUpperCase()).join("");
}
console.log(upper`hello ${"ann"} and ${"bob"}`, String.raw`a\`b`.length);
