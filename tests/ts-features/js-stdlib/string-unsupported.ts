// @feature: `String.prototype.normalize`, `localeCompare` with locales, `toLocaleUpperCase`
// @category: JavaScript: Standard Library
// @status: unsupported
// @why: not implemented yet: no Unicode normalization tables or locale data
// @instead: compare normalized input prepared by the host; `toUpperCase()` / `toLowerCase()`
// @error: 'normalize' is not supported
console.log("é".normalize("NFC").length);
