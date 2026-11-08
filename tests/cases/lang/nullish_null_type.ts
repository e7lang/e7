// a value of type null (or undefined) on the left of ??
function nul(): null { return null; }
function id<T>(x: T): T { return x; }
const a = nul() ?? 1;
const n: null = null;
const b = n ?? true;
const c = id(null) ?? id(0);
const u: undefined = undefined;
console.log(a, b, c, u ?? "s");
