// @feature: Ambient enums (`declare enum`)
// @category: Enums
// @status: unsupported
// @why: verifiable bytecode: only functions can be provided by the host
// @instead: an ordinary `enum`
// @error: unsupported declare statement
declare enum Platform { Ios, Android }
function name(p: Platform): string { return p === Platform.Ios ? "ios" : "android"; }
console.log(typeof name);
