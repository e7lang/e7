import { assert, assertEqual, assertClose } from "e7:test";
import { gcd, clamp } from "./math.ts";

export function testGcd(): void {
  assertEqual(gcd(12, 18), 6);
  assertEqual(gcd(7, 0), 7);
}
export function testClamp(): void {
  assertEqual(clamp(5, 0, 3), 3);
  assertEqual(clamp(-1, 0, 3), 0);
  console.log("clamp checked");
}
export function testFloat(): void {
  assertClose(0.1 + 0.2, 0.3);
}
export function testBoolean(): boolean {
  return gcd(9, 6) === 3;
}
// not a test: does not start with "test"
export function helper(): number { return 1; }
// not exported: not run
function testHidden(): void { assert(false, "must not run"); }
