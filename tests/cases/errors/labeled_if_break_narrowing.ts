// @error: object is possibly 'null'
// break lbl leaves the labeled if: after it, x may still be null
function g(x: string | null): number {
  lbl: if (x === null) { break lbl; }
  return x.length;
}
console.log(g(null));
