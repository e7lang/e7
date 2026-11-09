// @error: object is possibly 'undefined'
// `o?.v !== null` is true when o is undefined (o?.v is undefined then)
function f(o: { v: number } | undefined): number { if (o?.v !== null) return o.v; return 0; }
console.log(f(undefined));
