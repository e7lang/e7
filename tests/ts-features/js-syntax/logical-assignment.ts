// @feature: Logical assignment operators (`??=`, `||=`, `&&=`)
// @category: JavaScript: Syntax
// @status: supported
const cache = new Map<string, number[]>();
let list = cache.get("k");
list ??= [];
list.push(1);
let name: string | undefined;
name ??= "anonymous";
let count = 0;
count ||= 10;
let label = "x";
label &&= label.toUpperCase();
console.log(list, name, count, label);
