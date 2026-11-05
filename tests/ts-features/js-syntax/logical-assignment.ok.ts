// @feature: Logical assignment operators (`??=`, `||=`, `&&=`)
// @category: JavaScript: Syntax
// @status: partial
// @note: `??=`
const cache = new Map<string, number[]>();
let list = cache.get("k");
list ??= [];
list.push(1);
let name: string | undefined;
name ??= "anonymous";
console.log(list, name);
