// @feature: `RegExp`
// @category: JavaScript: Standard Library
// @status: unsupported
// @why: not implemented yet
// @instead: string methods and character code tests
// @error: 'RegExp' is not supported in E7
const re = new RegExp("(\\d+)-(\\d+)");
const m = re.exec("10-20");
console.log(m !== null ? m[1] : "");
