// Node.js implementation of the e7:json module. A JavaScript engine has no type information at
// runtime, so only syntax errors give null here; E7 also validates the shape against the asserted
// type (differential tests therefore use shape-correct documents or syntax errors only).
let last = "";

export function tryParse(text) {
  try {
    const v = JSON.parse(text);
    if (v === null) {
      last = "expected a value at $"; // E7: a document that is just `null` is a mismatch
      return null;
    }
    last = "";
    return v;
  } catch (e) {
    last = "SyntaxError: " + (e instanceof Error ? e.message : String(e));
    return null;
  }
}

export function lastError() {
  return last;
}
