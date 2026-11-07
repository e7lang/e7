// @feature: Regular expression literals
// @category: JavaScript: Syntax
// @status: unsupported
// @why: small runtime: a regular expression engine and its Unicode tables would add about 160 KB to every app
// @instead: string methods: `indexOf`, `includes`, `startsWith`, `endsWith`, `split`, `replaceAll`, `trim`
// @error: regular expressions are not supported in E7
const digits = /^[0-9]+$/;
console.log(digits.test("123"));
