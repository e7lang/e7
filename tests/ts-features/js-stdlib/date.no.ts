// @feature: `Date`
// @category: JavaScript: Standard Library
// @status: partial
// @why: JavaScript-identical behaviour: locale formatting (`toLocaleDateString`, `Intl`) depends on locale data that differs between platforms
// @instead: `toISOString()`, or format with `getUTCFullYear()` / `padStart`
// @error: method 'toLocaleDateString' does not exist on type 'Date'
const d = new Date(0);
d.setUTCFullYear(2000);
console.log(d.toISOString(), d.toLocaleDateString("en-US"));
