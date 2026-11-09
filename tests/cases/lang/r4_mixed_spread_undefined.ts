// fourth test round of 0.1.3 (mixed/spread_undefined): must print what Node.js prints
// Spread: a later source whose property is `T | undefined` and holds undefined does not
// overwrite an earlier source's non-nullable property (when the result type is inferred).
interface Patch { y: number | undefined }
const base = { x: 1, y: 2 };
const patch: Patch = { y: undefined };
const merged = { ...base, ...patch };
console.log(merged, merged.y, JSON.stringify(merged));          // node: y undefined, e7: y 2
const annotated: Patch = { ...base, ...patch };
console.log(annotated.y);                                        // both: undefined
console.log(Object.assign({ x: 1, y: 2 }, patch).y);            // node: undefined, e7: 2
class C { s: string | undefined = undefined; k = 1; }
console.log({ ...{ s: "x", k: 0 }, ...new C() });                // node: s undefined, e7: s 'x'
