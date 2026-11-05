// @error: not assignable to the optional property 'score?: number'
// a spread copies a property holding undefined: that sets it, so the same rule applies
interface Rec { id: number; score?: number }
interface Raw { score: number | undefined }
const raw: Raw = { score: undefined };
const r: Rec = { id: 1, ...raw };
console.log(r.id);
