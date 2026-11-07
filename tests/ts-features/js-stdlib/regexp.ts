// @feature: `RegExp`
// @category: JavaScript: Standard Library
// @status: unsupported
// @why: small runtime: a regular expression engine and its Unicode tables would add about 160 KB to every app
// @instead: string methods: `indexOf`, `includes`, `startsWith`, `endsWith`, `split`, `replaceAll`, `trim`
// @error: regular expressions are not supported in E7
const re = new RegExp("(\\d+)-(\\d+)");
console.log(re.test("10-20"));
