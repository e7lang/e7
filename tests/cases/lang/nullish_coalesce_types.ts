// `a ?? b` has type NonNullable<A> | B (e.g. arr.find(...) ?? null is T | null), as in TypeScript
const arr: number[] = [1, 2, 3];
const a: number | null = arr.find(x => x > 2) ?? null;
const b: number | null = arr.find(x => x > 5) ?? null;
let m = new Map<string, string>([["k", "v"]]);
const c: string | null = m.get("k") ?? null;
const d: string | null = m.get("z") ?? null;
function f(x?: number): number | null { return x ?? null; }
console.log(a, b, c, d, f(3), f(), a === null, b === null);
