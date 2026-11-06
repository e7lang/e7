// @feature: The comma operator
// @category: JavaScript: Syntax
// @status: supported
let a = 1;
const b = (a++, a * 10);
const log: string[] = [];
for (let i = 0, j = 3; i < j; i++, j--) log.push(`${i}${j}`);
console.log(b, log);
