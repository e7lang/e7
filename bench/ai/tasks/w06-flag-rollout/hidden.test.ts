import { assertEqual, assertThrows } from "e7:test";
import { fnv1a32, bucketOf, isEnabled, variantFor } from "./solution.ts";
import type { Flag } from "./solution.ts";

const users = ["alice", "bob", "carol", "dave", "erin", "frank", "grace", "heidi", "ivan", "judy"];

function flag(key: string, percent: number): Flag {
  return { key, enabled: true, rolloutPercent: percent, allowUsers: [], denyUsers: [] };
}

export function testFnvVectors(): void {
  assertEqual(fnv1a32("a"), 3826002220);
  assertEqual(fnv1a32("foobar"), 3214735720);
  assertEqual(fnv1a32("dark-mode:u1"), 3479154473); // above 2^31: must be unsigned
}

export function testFnvUsesUtf8(): void {
  assertEqual(fnv1a32("new-nav:zoë"), 1483006192);
}

export function testBuckets(): void {
  assertEqual(users.map((u) => bucketOf("checkout", u)), [1699, 7272, 5390, 2043, 5341, 5039, 2951, 8802, 7525, 7851]);
  assertEqual(bucketOf("", ""), 8253);
}

export function testRolloutThreshold(): void {
  // buckets: user-1522 -> 1249, user-10057 -> 1250
  assertEqual(isEnabled(flag("beta", 12.5), "user-1522"), true);
  assertEqual(isEnabled(flag("beta", 12.5), "user-10057"), false);
  assertEqual(isEnabled(flag("beta", 12.51), "user-10057"), true);
}

export function testRolloutUsesRoundedPercent(): void {
  // 1.1 * 100 is 110.00000000000001 in floating point; bucket 110 must stay out
  assertEqual(bucketOf("beta", "user-221"), 110);
  assertEqual(isEnabled(flag("beta", 1.1), "user-221"), false);
  assertEqual(isEnabled(flag("beta", 1.1), "user-25368"), true); // bucket 109
}

export function testZeroAndHundred(): void {
  // user-2649 has bucket 0, user-42552 has bucket 9999
  assertEqual(isEnabled(flag("beta", 0), "user-2649"), false);
  assertEqual(isEnabled(flag("beta", 0.01), "user-2649"), true);
  assertEqual(isEnabled(flag("beta", 100), "user-42552"), true);
  assertEqual(isEnabled(flag("beta", 99.99), "user-42552"), false);
}

export function testRolloutAcrossUsers(): void {
  const f = flag("checkout", 30);
  assertEqual(users.filter((u) => isEnabled(f, u)), ["alice", "dave", "grace"]);
}

export function testAllowDenyAndDisabled(): void {
  const f: Flag = { key: "checkout", enabled: true, rolloutPercent: 30, allowUsers: ["bob", "carol"], denyUsers: ["alice", "carol"] };
  assertEqual(users.filter((u) => isEnabled(f, u)), ["bob", "dave", "grace"]);
  f.enabled = false;
  assertEqual(users.filter((u) => isEnabled(f, u)), []);
  f.enabled = true;
  f.rolloutPercent = 100;
  assertEqual(isEnabled(f, "alice"), false);
}

export function testVariants(): void {
  const vs = [{ name: "control", weight: 50 }, { name: "blue", weight: 30 }, { name: "green", weight: 20 }];
  assertEqual(users.map((u) => variantFor("exp-color", u, vs)),
    ["green", "green", "control", "blue", "blue", "control", "control", "blue", "blue", "control"]);
}

export function testZeroWeightVariantNeverChosen(): void {
  const vs = [{ name: "s", weight: 1 }, { name: "m", weight: 0 }, { name: "l", weight: 3 }];
  assertEqual(users.map((u) => variantFor("exp-size", u, vs)).join(""), "slllssslsl");
}

export function testSevenWayModulo(): void {
  const vs = ["a", "b", "c", "d", "e", "f", "g"].map((name) => ({ name, weight: 1 }));
  assertEqual(users.map((u) => variantFor("pricing-test", u, vs)).join(""), "fafgaafadd");
}

export function testNoVariants(): void {
  assertThrows(() => { variantFor("x", "u", []); }, "no variants");
  assertThrows(() => { variantFor("x", "u", [{ name: "a", weight: 0 }]); }, "no variants");
}
