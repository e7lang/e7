// e7:json tryParse: success paths and syntax errors (compared with Node, where tryParse is
// JSON.parse in a try/catch). Shape mismatches are E7-only: see json_try_shape.ts.
import { tryParse, lastError } from "e7:json";
import * as json from "e7:json";

interface Item { id: number; name: string; tags: string[]; price?: number; meta: { created: string; flags: boolean[] } | null }
interface Order { orderId: string; items: Item[]; pair: [string, number] }

const text = `{
  "orderId": "A-17",
  "items": [
    {"id": 1, "name": "pen", "tags": ["office", "blue"], "price": 1.5, "meta": {"created": "2024-01-01", "flags": [true, false]}},
    {"id": 2, "name": "pad", "tags": [], "meta": null}
  ],
  "pair": ["x", 9]
}`;

function describe(o: Order | null): string {
  if (o === null) return "null";
  return o.orderId + " " + o.items.length + " " + o.items[0].tags.join("/") + " " + o.items[0].price + " " +
    (o.items[1].price === undefined) + " " + o.items[0].meta?.created + " " + (o.items[1].meta === null) + " " +
    o.pair[0] + o.pair[1];
}

const o = tryParse(text) as Order | null;
console.log(describe(o), JSON.stringify(lastError()));
console.log(JSON.stringify(o));

// syntax errors give null and a non-empty lastError()
const bad = ["", " ", "{", "[1,]", "{\"a\":1,}", "{'a':1}", "tru", "01", "1.", "-", "1e", "\"abc", "\"\\x\"",
  "\"\\u12\"", "[1] x", "{} {}", "\"a\nb\"", "NaN", "Infinity", "undefined", "\uFEFF1", "/* */ 1"];
for (const b of bad) {
  const r = tryParse(b) as Order | null;
  console.log(JSON.stringify(b), r === null, lastError() !== "");
}
// a success clears lastError()
const again = tryParse(text) as Order | null;
console.log(again !== null, lastError() === "");

// a document that is just `null` gives null (with an error message)
const nul = tryParse("null") as Order | null;
console.log(nul === null, lastError() !== "");
const nul2 = tryParse(" null ") as number | null;
console.log(nul2 === null, lastError() !== "");

// primitives, arrays, tuples, nullable elements
const n = tryParse("  42\n") as number | null;
console.log(n, lastError() === "");
const neg0 = tryParse("-0") as number | null;
if (neg0 !== null) console.log(1 / neg0);
const big = tryParse("[1e400, -1e400, 1e-400, 12345678901234567890, 0.1, 5e-324]") as number[] | null;
console.log(JSON.stringify(big), big !== null && big[0] === Infinity && big[1] === -Infinity);
const s = tryParse('"caf\\u00e9 \\ud83d\\ude00 \\/ \\t"') as string | null;
console.log(s, s?.length);
const lone = tryParse('"\\ud800x\\udc00"') as string | null;
console.log(lone?.length, lone?.charCodeAt(0), lone?.charCodeAt(2));
const raw = tryParse('"\u00e9\u4e2d"') as string | null;
console.log(raw, raw?.length);
const b = tryParse("false") as boolean | null;
console.log(b);
const nested = tryParse("[[1,2],[],[3]]") as number[][] | null;
console.log(JSON.stringify(nested));
const nullable = tryParse("[1,null,3]") as (number | null)[] | null;
console.log(JSON.stringify(nullable));
const tuple = tryParse('["k", 2, true]') as [string, number, boolean] | null;
if (tuple !== null) console.log(tuple[0], tuple[1], tuple[2]);
const dup = tryParse('{"id": 1, "id": 2}') as { id: number } | null;
console.log(dup?.id);
const extra = tryParse('{"id": 1, "unknown": [1, {"x": null}]}') as { id: number } | null;
console.log(extra?.id);

// optional fields
interface Opt { a?: number; b?: string | null; c: number | null }
const o1 = tryParse('{"c": null}') as Opt | null;
console.log(o1?.a === undefined, o1?.b === undefined, o1?.c === null);
const o2 = tryParse('{"a": 1, "b": null, "c": 3}') as Opt | null;
console.log(o2?.a, o2?.b === null, o2?.c);

// namespace import
const viaNs = json.tryParse("[true]") as boolean[] | null;
console.log(viaNs?.length, json.lastError() === "");
const viaNsBad = json.tryParse("[true") as boolean[] | null;
console.log(viaNsBad === null, json.lastError() !== "");

// the untrusted-input pattern
interface Config { host: string; port: number }
function loadConfig(t: string): Config {
  const c = tryParse(t) as Config | null;
  if (c === null) return { host: "localhost", port: 80 };
  return c;
}
console.log(JSON.stringify(loadConfig('{"host": "example.org", "port": 8080}')), JSON.stringify(loadConfig("{oops")));
