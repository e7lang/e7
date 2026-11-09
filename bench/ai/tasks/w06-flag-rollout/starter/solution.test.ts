import { assertEqual } from "e7:test";
import { fnv1a32, bucketOf, isEnabled } from "./solution.ts";
import type { Flag } from "./solution.ts";

export function testFnvVectors(): void {
  assertEqual(fnv1a32(""), 2166136261);
  assertEqual(fnv1a32("foobar"), 3214735720);
}

export function testBucket(): void {
  assertEqual(bucketOf("checkout-v2", "user-42"), 810);
}

export function testRollout(): void {
  const flag: Flag = { key: "checkout-v2", enabled: true, rolloutPercent: 10, allowUsers: [], denyUsers: [] };
  assertEqual(isEnabled(flag, "user-42"), true); // bucket 810 < 1000
}
