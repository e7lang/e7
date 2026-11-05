// @feature: `Proxy` and `Reflect`
// @category: JavaScript: Standard Library
// @status: unsupported
// @why: verifiable bytecode: property access is compiled for a fixed layout and cannot be intercepted
// @instead: explicit wrapper classes or methods
// @error: 'Proxy' is not supported in E7
interface Target { a: number }
const p = new Proxy<Target>({ a: 1 }, { get: (t, k) => (k === "a" ? t.a * 10 : 0) });
console.log(p.a, Reflect.has({ a: 1 }, "a"));
