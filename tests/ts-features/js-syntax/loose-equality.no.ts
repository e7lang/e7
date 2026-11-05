// @feature: Loose equality `==` / `!=`
// @category: JavaScript: Syntax
// @status: partial
// @why: types known at compile time: with static types, `==` is either `===` or a hidden type conversion
// @instead: `===` / `!==`
// @error: use '===' instead of '=='
const a: string = "1";
console.log(a == "1");
