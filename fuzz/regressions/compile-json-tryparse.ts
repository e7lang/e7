// e7:json tryParse in unusual positions: aliased and namespace imports, a shadowing local function,
// nested assertions, nullable and undefined targets, deep and malformed documents
import { tryParse as tp, lastError } from "e7:json";
import * as J from "e7:json";
interface P { a: number; b?: [string, (number | null)[]]; m: Map<string, { x: i32 } | null> | null }
const docs = ['{"a":1,"m":null}', '{"a":1,"b":["s",[1,null]],"m":{"k":{"x":2},"n":null}}', "[".repeat(400), "", "null", '{"a":"1"}'];
for (const d of docs) {
  const p = (tp(d) as P | null);
  const q = J.tryParse(d) as P | undefined | null;
  const r = ((tp(d)) as (P | null) | null);
  console.log(p === null, q === null, r === null, lastError(), J.lastError().length);
}
function shadow(): number {
  const tryParse = (s: string): number => s.length;
  return tryParse("abc");
}
console.log(shadow(), (tp("[[1],[2,3]]") as number[][] | null)?.length);
