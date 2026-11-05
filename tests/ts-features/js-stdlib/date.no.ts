// @feature: `Date`
// @category: JavaScript: Standard Library
// @status: partial
// @why: not implemented yet: `set*` methods other than `setTime`, and locale formatting
// @instead: compute with `Date.UTC(...)` and `setTime`
// @error: method 'setUTCFullYear' does not exist on type 'Date'
const d = new Date(0);
d.setUTCFullYear(2000);
console.log(d.toISOString(), d.toLocaleDateString("en-US"));
