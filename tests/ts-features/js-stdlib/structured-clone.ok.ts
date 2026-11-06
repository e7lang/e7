// @feature: `structuredClone`
// @category: JavaScript: Standard Library
// @status: partial
// @note: deep copies of arrays, object literals, `Map`, `Set`, `Date`, typed arrays and errors (shared and cyclic references kept)
const o = { a: [1, 2], m: new Map<string, Date>([["k", new Date(0)]]) };
const c = structuredClone(o);
c.a.push(3);
console.log(o, c);
