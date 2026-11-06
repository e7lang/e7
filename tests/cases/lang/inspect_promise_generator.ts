function* g(): Generator<number> { yield 1; }
const p = Promise.resolve(42);
const q = new Promise<string>(() => {});
console.log(p, q, g());
console.log([p], { g: g() });
const r = Promise.reject(new Error("x"));
r.catch(() => {});
console.log(String(p), typeof p);
