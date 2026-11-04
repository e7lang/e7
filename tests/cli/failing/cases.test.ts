import { assert, assertEqual, assertThrows } from "e7:test";

let counter = 0;

export function testPasses(): void { assert(true); }
export function testAssertEqualFails(): void {
  console.log("before the failure");
  assertEqual([1, 2], [1, 3], "lists");
}
export function testReturnsFalse(): boolean { return false; }
export function testThrows(): void { throw new Error("thrown on purpose"); }
export function testIndexError(): void { const a: number[] = []; console.log(a[2]); }
export function testInfiniteLoop(): void { while (true) { counter++; } }
export function testOutOfMemory(): void {
  const parts: string[] = [];
  let s = "xxxxxxxxxxxxxxxx";
  while (true) { s = s + s; parts.push(s); }
}
function deep(n: number): number { return deep(n + 1) + 1; }
export function testStackOverflow(): void { console.log(deep(0)); }
export function testIsolationA(): void { counter++; assertEqual(counter, 1); }
export function testIsolationB(): void { counter++; assertEqual(counter, 1); }
export function testWithParam(x: number): void { assert(x > 0); }
export function testReturnsNumber(): number { return 1; }
export function testAssertThrowsPasses(): void { assertThrows(() => { throw new Error("boom"); }, "boom"); }
