// e7:json tryParse: type validation (E7-only; a JavaScript engine cannot check the shape, so the
// expected output is in json_try_shape.out).
import { tryParse, lastError } from "e7:json";

interface Payment { id: string; amount: number; paid: boolean; note?: string; tags: string[]; to: { iban: string } | null }

function show(label: string, ok: boolean): void {
  console.log(label + ": " + (ok ? "ok" : "null") + " " + JSON.stringify(lastError()));
}

const good = '{"id": "p1", "amount": 12.5, "paid": true, "tags": ["a"], "to": {"iban": "DE00"}}';
show("good", (tryParse(good) as Payment | null) !== null);
show("amount as string", (tryParse('{"id": "p1", "amount": "12.5", "paid": true, "tags": [], "to": null}') as Payment | null) !== null);
show("missing id", (tryParse('{"amount": 1, "paid": true, "tags": [], "to": null}') as Payment | null) !== null);
show("paid as number", (tryParse('{"id": "p", "amount": 1, "paid": 1, "tags": [], "to": null}') as Payment | null) !== null);
show("note as number", (tryParse('{"id": "p", "amount": 1, "paid": true, "note": 5, "tags": [], "to": null}') as Payment | null) !== null);
show("tags[1] as number", (tryParse('{"id": "p", "amount": 1, "paid": true, "tags": ["x", 2], "to": null}') as Payment | null) !== null);
show("tags as object", (tryParse('{"id": "p", "amount": 1, "paid": true, "tags": {}, "to": null}') as Payment | null) !== null);
show("to missing", (tryParse('{"id": "p", "amount": 1, "paid": true, "tags": []}') as Payment | null) !== null);
show("to.iban missing", (tryParse('{"id": "p", "amount": 1, "paid": true, "tags": [], "to": {}}') as Payment | null) !== null);
show("root is array", (tryParse("[]") as Payment | null) !== null);
show("root is null", (tryParse("null") as Payment | null) !== null);
show("again good", (tryParse(good) as Payment | null) !== null);

// unknown fields are dropped (the result has exactly the declared fields)
const extra = tryParse('{"id": 1, "unknown": [1, {"x": null}]}') as { id: number } | null;
console.log(JSON.stringify(extra));

// nullable T: `(T | null) | null` is the same type as `T | null`, so a JSON null document is
// still reported as a mismatch; lastError() tells the cases apart
type MaybeNum = number | null;
const mn = tryParse("null") as MaybeNum | null;
show("nullable T, null document", mn !== null);
const mn2 = tryParse("[null]") as MaybeNum[] | null;
console.log(JSON.stringify(mn2), JSON.stringify(lastError()));

// optional (undefined) T
const ou = tryParse("7") as number | undefined | null;
console.log(ou, JSON.stringify(lastError()));

// tuples
show("tuple ok", (tryParse('["a", 1]') as [string, number] | null) !== null);
show("tuple too short", (tryParse('["a"]') as [string, number] | null) !== null);
show("tuple too long", (tryParse('["a", 1, 2]') as [string, number] | null) !== null);
show("tuple element", (tryParse('["a", "1"]') as [string, number] | null) !== null);
show("tuple as object", (tryParse('{"0": "a", "1": 1}') as [string, number] | null) !== null);

// Map<string, V>
const m = tryParse('{"net": 10.5, "tax": 2.1, "a.b": 0}') as Map<string, number> | null;
console.log(m?.size, m?.get("net"), m?.get("a.b"), JSON.stringify(lastError()));
show("map value", (tryParse('{"net": 10.5, "tax": "2.1"}') as Map<string, number> | null) !== null);
show("map as array", (tryParse("[1]") as Map<string, number> | null) !== null);
const mm = tryParse('{"a": {"x": [1, 2]}, "b": {}}') as Map<string, Map<string, number[]>> | null;
console.log(mm?.get("a")?.get("x")?.length, mm?.get("b")?.size);
show("nested map", (tryParse('{"a": {"x": [1, "2"]}}') as Map<string, Map<string, number[]>> | null) !== null);
const mo = tryParse('{"a": null, "b": 1}') as Map<string, number | null> | null;
console.log(mo?.get("a") === null, mo?.get("b"));

// fixed-width integers
show("i32 ok", (tryParse("-2147483648") as i32 | null) !== null);
show("i32 fraction", (tryParse("1.5") as i32 | null) !== null);
show("i32 too big", (tryParse("2147483648") as i32 | null) !== null);
show("u8 negative", (tryParse("[1, -1]") as u8[] | null) !== null);
show("i64 1e400", (tryParse("1e400") as i64 | null) !== null);
const iv = tryParse("[255, 0]") as u8[] | null;
if (iv !== null) console.log(iv[0], iv[1]);

// numbers
const huge = tryParse("[1e308, 1e309, -1e309, 1e-400, 123456789012345678901234567890]") as number[] | null;
if (huge !== null) console.log(JSON.stringify(huge), huge[1]);

// syntax errors: message and position
const syntax = ["", "   ", "{", "[1,]", "[1 2]", "{\"a\" 1}", "{a: 1}", "tru", "nul", "01", "1.", "-", "1e+",
  "\"abc", "\"\\x\"", "\"\\u12G4\"", "\"a\tb\"", "[1] x", "{} {}", "NaN", "'a'", "\uFEFF1"];
for (const t of syntax) {
  const r = tryParse(t) as number[] | null;
  console.log(JSON.stringify(t), r === null, lastError());
}
// whitespace
const ws = tryParse(" \t\r\n[ 1 , 2 ]\n\t ") as number[] | null;
console.log(JSON.stringify(ws), JSON.stringify(lastError()));

// strings: escapes, unicode, lone surrogates (kept, as in JavaScript)
const es = tryParse('["\\u00E9\\u00e9", "\\ud83d\\ude00", "\\ud800", "\\udc00x", "\u{1F600}", "\\"\\\\\\/\\b\\f\\n\\r\\t"]') as string[] | null;
if (es !== null) {
  console.log(es[0], es[1], es[1].length, es[2].length, es[2].charCodeAt(0), es[3].charCodeAt(0), es[4].length);
  console.log(JSON.stringify(es[5]));
}
const raw = tryParse("\"" + String.fromCharCode(0xd800) + "\"") as string | null;
console.log(raw?.length, raw?.charCodeAt(0), JSON.stringify(lastError()));
const keyPath = tryParse('{"caf\\u00e9": "x"}') as Map<string, number> | null;
console.log(keyPath === null, lastError());

// deep nesting: limited to 256 levels, never a crash
const deepOk = tryParse("[".repeat(255) + "]".repeat(255)) as number[] | null;
console.log(deepOk === null, lastError());
const deep = tryParse("[".repeat(300) + "]".repeat(300)) as number[] | null;
console.log(deep === null, lastError());
const deeper = tryParse("[".repeat(100000)) as number[] | null;
console.log(deeper === null, lastError());
const deepObj = tryParse('{"a":'.repeat(5000) + "1" + "}".repeat(5000)) as Map<string, number> | null;
console.log(deepObj === null, lastError());

// many failures and successes in a loop (garbage from partly built results)
let nulls = 0;
for (let i = 0; i < 2000; i++) {
  const doc = '{"id": "p' + i + '", "amount": ' + (i % 3 === 0 ? '"x"' : String(i)) + ', "paid": true, "tags": ["t"], "to": null}';
  const p = tryParse(doc) as Payment | null;
  if (p === null) nulls++;
  else if (p.amount !== i) console.log("wrong", i);
}
console.log(nulls, JSON.stringify(lastError()));

// JSON.parse is unchanged and does not touch lastError()
const fail = tryParse("[") as number[] | null;
const jp = JSON.parse("[1]") as number[];
console.log(fail === null, jp.length, lastError());
