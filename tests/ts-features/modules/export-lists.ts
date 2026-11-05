// @feature: Export lists (`export { a, b as c }`)
// @category: Modules
// @status: supported
const version = "1.0";
function major(): number { return Number(version.split(".")[0]); }
export { version, major as majorVersion };
console.log(version, major());
