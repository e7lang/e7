// @feature: Error types (`Error`, `TypeError`, `RangeError`, `SyntaxError`, `URIError`)
// @category: JavaScript: Standard Library
// @status: partial
// @note: construction (also with `{ cause }`), `name` / `message` / `cause` (also setting `name` and `message`), `AggregateError` and its `errors`, `ReferenceError` / `EvalError`, `instanceof`, the error classes as types (they are all `Error`), catching errors raised by the standard library
const errors: Error[] = [new Error("e"), new TypeError("t"), new RangeError("r"), new SyntaxError("s"), new URIError("u")];
for (const e of errors) console.log(e.name, e.message, String(e), e instanceof Error);
try {
  decodeURIComponent("%");
} catch (e) {
  if (e instanceof URIError) console.log("URIError caught");
}
const wrapped = new Error("outer", { cause: new Error("inner") });
const all = new AggregateError([new Error("a"), new Error("b")], "two");
console.log(wrapped.cause instanceof Error ? wrapped.cause.message : "", all.name, all.errors.length);
const renamed = new Error("m");
renamed.name = "MyError";
renamed.message = "changed";
const asType: TypeError = new TypeError("t");
console.log(String(renamed), asType.name, new ReferenceError("r") instanceof ReferenceError);
