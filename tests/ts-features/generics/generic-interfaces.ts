// @feature: Generic interfaces and type aliases
// @category: Generics
// @status: supported
interface Pair<A, B> { first: A; second: B }
type Result<T> = { ok: boolean; value: T | null; error?: string };
function ok<T>(v: T): Result<T> { return { ok: true, value: v }; }
const p: Pair<number, string> = { first: 1, second: "one" };
const r: Result<number[]> = ok([1, 2]);
const e: Result<string> = { ok: false, value: null, error: "boom" };
console.log(p, r, e);
