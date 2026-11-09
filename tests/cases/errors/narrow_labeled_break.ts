// @error: object is possibly 'null'
// a labeled block left by `break` does not reach its end
class X { v = 1; }
let x: X | null = null;
const n = Math.random();
blk: { if (n > 2) break blk; x = new X(); }
console.log(x.v);
