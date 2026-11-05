// @feature: `structuredClone`
// @category: JavaScript: Standard Library
// @status: unsupported
// @why: not implemented yet
// @instead: object spread `{ ...o }` / `[...xs]`, or `JSON.parse(JSON.stringify(o)) as T`
// @error: cannot find name 'structuredClone'
const o = { a: [1, 2] };
const c = structuredClone(o);
console.log(c);
