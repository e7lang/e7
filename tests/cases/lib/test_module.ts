// e7:test assertion helpers (passing assertions; checked against Node via the shim)
import { assert, assertEqual, assertNotEqual, assertClose, assertThrows } from "e7:test";

class Point {
  constructor(public x: number, public y: number) {}
}
interface Item { name: string; tags: string[]; qty: number | null }
enum Dir { Up, Down }
enum Mode { Fast = "fast", Slow = "slow" }

assert(1 + 1 === 2);
assert("abc".length === 3, "string length");
assertEqual(1 + 2, 3);
assertEqual(3, 1 + 2);
const small: i32 = 7;
assertEqual(small, 7);
assertEqual(7, small);
assertEqual(u8(300), 44);
assertEqual("ab" + "c", "abc", "concatenation");
assertEqual(!false, true);
assertEqual(2n ** 70n, 1180591620717411303424n);
assertEqual(new Uint8Array([1, 2, 3]), new Uint8Array([1, 2, 3]));
assertEqual([1, 2, 3].map((x) => x * 2), [2, 4, 6]);
assertEqual([[1], [2, 3]], [[1], [2, 3]]);
assertEqual(["a", "b"], ["a", "b"]);
assertEqual(new Point(1, 2), new Point(1, 2));
const it: Item = { name: "n", tags: ["x", "y"], qty: null };
assertEqual(it, { name: "n", tags: ["x", "y"], qty: null });
assertEqual(new Map<string, number>([["a", 1], ["b", 2]]), new Map<string, number>([["b", 2], ["a", 1]]));
assertEqual(new Set<string>(["p", "q"]), new Set<string>(["q", "p"]));
const none: string | null = null;
assertEqual(none, null);
const maybe: number | undefined = 5;
assertEqual(maybe, 5);
assertEqual(NaN, NaN);
assertEqual(Dir.Down, Dir.Down);
assertEqual(Mode.Slow, Mode.Slow);
assertEqual(JSON.stringify({ a: [1, 2] }), '{"a":[1,2]}');
const empty: number[] = [];
assertEqual(empty, []);

assertNotEqual(1, 2);
assertNotEqual("a", "A");
assertNotEqual([1, 2], [1, 2, 3]);
assertNotEqual(new Point(1, 2), new Point(2, 1), "points differ");
assertNotEqual(new Map<string, number>([["a", 1]]), new Map<string, number>([["a", 2]]));

assertClose(0.1 + 0.2, 0.3);
assertClose(Math.PI, 3.14159, 1e-5, "pi");

assertThrows(() => { throw new Error("bad input: 42"); }, "bad input");
assertThrows(() => { abort("stop"); });
assertThrows(() => assertEqual(1, 2), "assertEqual failed");
assertThrows(() => assert(false, "inner"), "assertion failed: inner");
assertThrows(() => { assertThrows(() => {}); }, "did not throw");

let counter = 0;
assertThrows(() => { counter++; throw new Error("x"); });
assertEqual(counter, 1);
console.log("all assertions passed", counter);
