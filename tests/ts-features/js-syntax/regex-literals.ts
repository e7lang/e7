// @feature: Regular expression literals
// @category: JavaScript: Syntax
// @status: supported
// @note: checked when compiling, with V8's error messages; flags `dgimsuy` (the `v` flag is not supported)
const digits = /^[0-9]+$/;
console.log(digits.test("123"), "a1b2".replace(/[0-9]/g, "#"), /a\/b/.source, digits);
