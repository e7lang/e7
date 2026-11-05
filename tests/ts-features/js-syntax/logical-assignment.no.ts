// @feature: Logical assignment operators (`??=`, `||=`, `&&=`)
// @category: JavaScript: Syntax
// @status: partial
// @why: not implemented yet: `||=` and `&&=`
// @instead: `x = x || y` / `if (x) x = y`
// @error: '||=' is not supported
let retries = 0;
retries ||= 3;
let enabled = true;
enabled &&= retries > 2;
console.log(retries, enabled);
