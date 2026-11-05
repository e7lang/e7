// @error: type 'number | undefined' is not assignable to the optional property 'score?: number'
interface Rec { id: number; score?: number }
function last(xs: number[]): number | undefined { return xs.length > 0 ? xs[xs.length - 1] : undefined; }
const r: Rec = { id: 1 };
r.score = last([]);
console.log(r.id);
