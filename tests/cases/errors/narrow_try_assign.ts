// @error: object is possibly 'null'
// an assignment in a try block does not narrow after it (the catch block may run instead)
class X { v = 1; }
function mk(): X { return new X(); }
let x: X | null = null;
try { x = mk(); } catch { }
console.log(x.v);
