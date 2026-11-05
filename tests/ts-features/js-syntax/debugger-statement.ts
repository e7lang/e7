// @feature: `debugger` statement
// @category: JavaScript: Syntax
// @status: supported
// @note: stops when a debugger is attached
let total = 0;
for (let i = 1; i <= 3; i++) {
  total += i;
  if (i === 2) debugger;
}
console.log(total);
