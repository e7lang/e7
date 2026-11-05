// @feature: Regular expression literals
// @category: JavaScript: Syntax
// @status: unsupported
// @why: not implemented yet
// @instead: string methods (`indexOf`, `includes`, `startsWith`, `split`, `replaceAll`) and character code tests
// @error: regular expressions are not supported
const digits = /^[0-9]+$/;
console.log(digits.test("123"), "a1b2".replace(/[0-9]/g, "#"));
