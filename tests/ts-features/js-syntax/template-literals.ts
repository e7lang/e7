// @feature: Template literals (multi-line, nested)
// @category: JavaScript: Syntax
// @status: supported
const name = "Ann";
const items = [1, 2];
const msg = `Hello ${name}, you have ${items.length} item${items.length === 1 ? "" : "s"}:
${items.map(i => `  - #${i}`).join("\n")}`;
console.log(msg, `${1 + 1}${"x"}${null}${undefined}${[1, 2]}${true}`);
