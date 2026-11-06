// @feature: `RegExp`
// @category: JavaScript: Standard Library
// @status: partial
// @why: types known at compile time: the named groups of a pattern built at run time are not known, so its `groups` object has no properties to read
// @instead: a regular expression literal (its groups are typed), or the indexed groups `m[1]`, `m[2]`
// @error: groups of a pattern built at run time are not known at compile time
const re = new RegExp("(?<year>\\d{4})");
console.log(re.exec("2024")?.groups);
