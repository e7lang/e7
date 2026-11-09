import { assertEqual, assertThrows } from "e7:test";
import { LruCache, getOrCompute } from "./solution.ts";

export function testEvictsLeastRecentlyUsed(): void {
  const c = new LruCache<string, number>(2);
  c.set("a", 1);
  c.set("b", 2);
  assertEqual(c.get("a"), 1);
  c.set("c", 3);
  assertEqual(c.keys(), ["c", "a"]);
  assertEqual(c.get("b"), undefined);
  assertEqual(c.stats(), { hits: 1, misses: 1, evictions: 1 });
}

export function testUpdateAndDelete(): void {
  const c = new LruCache<number, string>(3);
  c.set(1, "one");
  c.set(2, "two");
  c.set(1, "uno");
  assertEqual(c.keys(), [1, 2]);
  assertEqual(c.delete(2), true);
  assertEqual(c.delete(2), false);
  assertEqual(c.size, 1);
}

export function testGetOrCompute(): void {
  const c = new LruCache<string, number>(4);
  let calls = 0;
  const len = (k: string): number => { calls++; return k.length; };
  assertEqual(getOrCompute(c, "hello", len), 5);
  assertEqual(getOrCompute(c, "hello", len), 5);
  assertEqual(calls, 1);
}

export function testBadCapacity(): void {
  assertThrows(() => { new LruCache<string, string>(0); }, "capacity must be a positive integer");
}
