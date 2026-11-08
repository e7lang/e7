// @runtime-error: expected "percent" | "tier" at $.kind
type Kind = "percent" | "tier";
interface Rule { kind: Kind; value: number }
const r = JSON.parse(`{"kind":"mystery","value":5}`) as Rule;
console.log(r.kind);
