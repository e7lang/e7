// @feature: `JSON.stringify` / `JSON.parse`
// @category: JavaScript: Standard Library
// @status: partial
// @note: `JSON.parse(text) as T` validates the text against `T` while parsing (literal types and enums too)
interface Item { id: number; name: string; tags?: string[]; price: number | null }
const items = JSON.parse('[{"id":1,"name":"pen","price":2.5},{"id":2,"name":"ink","tags":["blue"],"price":null}]') as Item[];
console.log(items[1].tags, items[0].price, items.length);
console.log(JSON.stringify(items), JSON.stringify({ a: [{ b: 2 }, { b: 3 }] }, null, 2));
interface Doc { u?: number; n: number | null; d: Date }
const doc: Doc = { n: null, d: new Date(0) };
console.log(JSON.stringify("q\"\n"), JSON.stringify(doc), JSON.stringify([NaN, Infinity]), JSON.stringify([1, 2], null, "\t"));
enum Level { Low = 1, High = 2 }
interface Setting { mode: "fast" | "safe"; level: Level }
const st = JSON.parse('{"mode":"safe","level":2}') as Setting;
console.log(st.mode, st.level === Level.High);
