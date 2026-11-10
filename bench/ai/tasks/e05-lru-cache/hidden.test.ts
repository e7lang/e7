import { assert, assertEqual, assertThrows } from "e7:test";
import { LruCache, getOrCompute } from "./solution.ts";

interface Blob {
  id: string;
  bytes: number;
}

function blob(id: string, bytes: number): Blob {
  return { id, bytes };
}

export function testCapacityStillWorksWithoutOptions(): void {
  const c = new LruCache<string, number>(2);
  assertEqual(c.set("a", 1), true);
  c.set("b", 2);
  c.get("a");
  c.set("c", 3);
  assertEqual(c.keys(), ["c", "a"]);
  assertEqual(c.weight, 2);
  assertEqual(c.stats(), { hits: 1, misses: 0, evictions: 1 });
}

export function testWeightLimitEvictsLeastRecentlyUsed(): void {
  const c = new LruCache<string, Blob>(10, { maxWeight: 100, weigh: (b) => b.bytes });
  c.set("a", blob("a", 40));
  c.set("b", blob("b", 30));
  c.set("c", blob("c", 20));
  assertEqual(c.weight, 90);
  c.get("a");
  c.set("d", blob("d", 50)); // 140 > 100: evict b (30) then c (20)
  assertEqual(c.keys(), ["d", "a"]);
  assertEqual(c.weight, 90);
  assertEqual(c.stats().evictions, 2);
}

export function testTooHeavyIsRejectedAndChangesNothing(): void {
  const c = new LruCache<string, Blob>(10, { maxWeight: 100, weigh: (b) => b.bytes });
  c.set("a", blob("a", 60));
  c.set("b", blob("b", 10));
  assertEqual(c.set("a", blob("a2", 101)), false);
  assertEqual(c.set("z", blob("z", 500)), false);
  assertEqual(c.keys(), ["b", "a"]);
  assertEqual(c.peek("a"), blob("a", 60));
  assertEqual(c.weight, 70);
  assertEqual(c.stats(), { hits: 0, misses: 0, evictions: 0 });
  assertEqual(c.set("y", blob("y", 100)), true); // exactly maxWeight fits, evicting everything else
  assertEqual(c.keys(), ["y"]);
}

export function testUpdateChangesWeight(): void {
  const c = new LruCache<string, string>(5, { maxWeight: 10, weigh: (s) => s.length });
  c.set("x", "aaaa");
  c.set("y", "bbbb");
  c.set("x", "a"); // x shrinks: 1 + 4
  assertEqual(c.weight, 5);
  c.set("y", "bbbbbbbbb"); // y grows to 9: 10 total, still fits
  assertEqual(c.keys(), ["y", "x"]);
  c.set("x", "aa"); // 11: evict y, never the entry just set
  assertEqual(c.keys(), ["x"]);
  assertEqual(c.weight, 2);
}

export function testOnEvictOrderAndArguments(): void {
  const log: string[] = [];
  const c = new LruCache<number, string>(2, { onEvict: (k, v) => { log.push(`${k}=${v}`); } });
  c.set(1, "one");
  c.set(2, "two");
  c.set(3, "three");
  c.set(4, "four");
  assertEqual(log, ["1=one", "2=two"]);
}

export function testOnEvictNotCalledForDeleteOrOverwrite(): void {
  const log: string[] = [];
  const c = new LruCache<string, number>(3, { onEvict: (k, v) => { log.push(k); } });
  c.set("a", 1);
  c.set("a", 2);
  c.set("b", 1);
  c.delete("a");
  assertEqual(log, []);
  assertEqual(c.weight, 1);
}

export function testOnEvictRunsAfterRemoval(): void {
  const seen: string[] = [];
  let cache: LruCache<string, Blob> | null = null;
  const c = new LruCache<string, Blob>(10, {
    maxWeight: 10,
    weigh: (b) => b.bytes,
    onEvict: (k, v) => {
      if (cache !== null) seen.push(`${k} size=${cache.size} has=${cache.has(k)} weight=${cache.weight}`);
    },
  });
  cache = c;
  c.set("a", blob("a", 6));
  c.set("b", blob("b", 6));
  assertEqual(seen, ["a size=1 has=false weight=6"]);
}

export function testEvictionsForCapacityAndWeightBothCount(): void {
  const evicted: string[] = [];
  const c = new LruCache<string, Blob>(2, { maxWeight: 10, weigh: (b) => b.bytes, onEvict: (k, v) => { evicted.push(v.id); } });
  c.set("a", blob("a", 1));
  c.set("b", blob("b", 1));
  c.set("c", blob("c", 1)); // over capacity: a goes
  c.set("d", blob("d", 9)); // over capacity: b goes; c + d weigh 10, which fits
  assertEqual(evicted, ["a", "b"]);
  assertEqual(c.keys(), ["d", "c"]);
  c.set("e", blob("e", 5)); // capacity evicts c, then weight (14) evicts d
  assertEqual(evicted, ["a", "b", "c", "d"]);
  assertEqual(c.keys(), ["e"]);
  assertEqual(c.stats().evictions, 4);
}

export function testPeekDoesNotTouchRecencyOrStats(): void {
  const c = new LruCache<string, number>(2);
  c.set("a", 1);
  c.set("b", 2);
  assertEqual(c.peek("a"), 1);
  assertEqual(c.peek("zz"), undefined);
  c.set("c", 3); // a is still the least recently used
  assertEqual(c.keys(), ["c", "b"]);
  assertEqual(c.stats(), { hits: 0, misses: 0, evictions: 1 });
}

export function testInvalidOptions(): void {
  assertThrows(() => { new LruCache<string, number>(3, { maxWeight: 0 }); }, "maxWeight must be positive");
  assertThrows(() => { new LruCache<string, number>(0, { maxWeight: 5 }); }, "capacity must be a positive integer");
  const c = new LruCache<string, number>(3, { weigh: (n) => n });
  assertThrows(() => { c.set("neg", -1); }, "invalid weight");
  assertThrows(() => { c.set("nan", NaN); }, "invalid weight");
  assertEqual(c.size, 0);
}

export function testGetOrComputeWithWeights(): void {
  const c = new LruCache<string, string>(10, { maxWeight: 8, weigh: (s) => s.length });
  let calls = 0;
  const up = (k: string): string => { calls++; return k.toUpperCase(); };
  assertEqual(getOrCompute(c, "hello", up), "HELLO");
  assertEqual(getOrCompute(c, "hello", up), "HELLO");
  assertEqual(getOrCompute(c, "too long!", up), "TOO LONG!"); // not cached
  assertEqual(getOrCompute(c, "too long!", up), "TOO LONG!");
  assertEqual(calls, 3);
  assert(!c.has("too long!"));
  assertEqual(c.stats(), { hits: 1, misses: 3, evictions: 0 });
}
