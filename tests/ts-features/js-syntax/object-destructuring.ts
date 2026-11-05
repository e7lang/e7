// @feature: Object destructuring (renaming, defaults, rest, nested)
// @category: JavaScript: Syntax
// @status: supported
interface Opts { host?: string; port: number; tls: boolean; meta: { tags: string[] } }
const o: Opts = { port: 80, tls: false, meta: { tags: ["a", "b"] } };
const { host = "localhost", port: p, meta: { tags: [firstTag] }, ...others } = o;
console.log(host, p, firstTag, others);
