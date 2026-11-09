// @error: object is possibly 'undefined'
// a link after '?.' on a value that is never null still checks its own object (`i.o` is possibly undefined)
interface I { o?: { x: number } }
const i: I = {};
console.log(i?.o.x);
