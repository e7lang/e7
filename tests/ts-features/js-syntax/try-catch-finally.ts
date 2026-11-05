// @feature: `try` / `catch` / `finally`, optional catch binding
// @category: JavaScript: Syntax
// @status: supported
// @note: catches the errors Node.js also throws; E7's own checks (out-of-bounds reads, failed `as`) are not catchable
function parse(text: string): number {
  try {
    return JSON.parse(text) as number;
  } catch (e) {
    if (e instanceof SyntaxError) return -1;
    throw e;
  } finally {
    console.log("parsed", text);
  }
}
console.log(parse("41"), parse("{"));
try {
  (12.5).toFixed(200);
} catch {
  console.log("toFixed failed");
}
try {
  throw new TypeError("bad type");
} catch (e) {
  if (e instanceof Error) console.log(String(e), e.name, e.message);
}
