// @error: not assignable to the optional property 'score?: number'
// exactOptionalPropertyTypes: an optional property is left out, not set to undefined
interface Rec { id: number; score?: number }
const r: Rec = { id: 1, score: undefined };
console.log(r.id);
