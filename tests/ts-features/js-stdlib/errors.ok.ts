// @feature: Error types (`Error`, `TypeError`, `RangeError`, `SyntaxError`, `URIError`)
// @category: JavaScript: Standard Library
// @status: partial
// @note: construction, `name` / `message`, `instanceof`, catching errors raised by the standard library
const errors: Error[] = [new Error("e"), new TypeError("t"), new RangeError("r"), new SyntaxError("s"), new URIError("u")];
for (const e of errors) console.log(e.name, e.message, String(e), e instanceof Error);
try {
  decodeURIComponent("%");
} catch (e) {
  if (e instanceof URIError) console.log("URIError caught");
}
try {
  "x".repeat(-1);
} catch (e) {
  if (e instanceof RangeError) console.log("RangeError caught");
}
