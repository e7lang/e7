// third test round of 0.1.3 (error_name_instanceof): must print what Node.js prints
const t = new TypeError("x");
t.name = "RangeError";
console.log(t instanceof TypeError, t instanceof RangeError);
const e = new Error("y");
e.name = "SyntaxError";
console.log(e instanceof SyntaxError);
try { throw e; } catch (c) { console.log(c instanceof SyntaxError ? "syntax" : "other"); }
