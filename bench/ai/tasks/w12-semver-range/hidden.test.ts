import { assertEqual, assertThrows } from "e7:test";
import { parseVersion, compareVersions, satisfies, maxSatisfying } from "./solution.ts";

export function testParseValid(): void {
  assertEqual(parseVersion("0.0.0"), { major: 0, minor: 0, patch: 0, prerelease: [] });
  assertEqual(parseVersion(" v10.20.30 "), { major: 10, minor: 20, patch: 30, prerelease: [] });
  assertEqual(parseVersion("1.0.0-0.3.7"), { major: 1, minor: 0, patch: 0, prerelease: ["0", "3", "7"] });
  assertEqual(parseVersion("1.0.0-x-y.7z.--"), { major: 1, minor: 0, patch: 0, prerelease: ["x-y", "7z", "--"] });
  assertEqual(parseVersion("1.0.0+20130313144700"), { major: 1, minor: 0, patch: 0, prerelease: [] });
  assertEqual(parseVersion("1.0.0-alpha+001"), { major: 1, minor: 0, patch: 0, prerelease: ["alpha"] });
}

export function testParseInvalid(): void {
  for (const bad of ["", "1", "1.2", "1.2.3.4", "01.2.3", "1.02.3", "1.2.03", "1.2.3-", "1.2.3+", "1.2.3-01",
                     "1.2.3-a..b", "1.2.3-a_b", "-1.2.3", "1.2.x", "vv1.2.3", "1.2.3 4", "1.2.3+a+b", "a.b.c"]) {
    assertThrows(() => { parseVersion(bad); }, `invalid version: ${bad}`);
  }
}

export function testPrereleaseOrder(): void {
  const order = ["1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2",
                 "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0"];
  for (let i = 0; i + 1 < order.length; i++) {
    assertEqual(compareVersions(order[i], order[i + 1]), -1, order[i]);
    assertEqual(compareVersions(order[i + 1], order[i]), 1, order[i]);
  }
  assertEqual(compareVersions("1.0.0-1", "1.0.0-a"), -1);
  assertEqual(compareVersions("1.0.0-B", "1.0.0-a"), -1);
  assertEqual(compareVersions("1.0.0-rc.1", "0.9.9"), 1);
}

export function testCoreOrderAndEquality(): void {
  assertEqual(compareVersions("2.0.0", "10.0.0"), -1);
  assertEqual(compareVersions("1.2.10", "1.2.9"), 1);
  assertEqual(compareVersions("1.0.0+a", "1.0.0+b"), 0);
  assertEqual(compareVersions("v1.0.0", "1.0.0"), 0);
  assertEqual(compareVersions("1.0.0-beta.2+x", "1.0.0-beta.2"), 0);
  assertThrows(() => { compareVersions("1.0", "1.0.0"); }, "invalid version: 1.0");
}

export function testSimpleComparators(): void {
  assertEqual(satisfies("1.2.3", "1.2.3"), true);
  assertEqual(satisfies("1.2.3+build", "=1.2.3"), true);
  assertEqual(satisfies("1.2.4", "=1.2.3"), false);
  assertEqual(satisfies("1.2.3", ">1.2.3"), false);
  assertEqual(satisfies("1.2.3", ">=1.2.3"), true);
  assertEqual(satisfies("1.2.2", "<1.2.3"), true);
  assertEqual(satisfies("1.2.3", "<=1.2.3"), true);
  assertEqual(satisfies("1.2.4", "<=1.2.3"), false);
  assertEqual(satisfies("99.0.0", "*"), true);
}

export function testCaretAndTilde(): void {
  assertEqual(["1.2.3", "1.9.0", "2.0.0", "1.2.2"].map((v) => satisfies(v, "^1.2.3")), [true, true, false, false]);
  assertEqual(["0.2.3", "0.2.9", "0.3.0"].map((v) => satisfies(v, "^0.2.3")), [true, true, false]);
  assertEqual(["0.0.3", "0.0.4"].map((v) => satisfies(v, "^0.0.3")), [true, false]);
  assertEqual(["1.2.3", "1.2.99", "1.3.0"].map((v) => satisfies(v, "~1.2.3")), [true, true, false]);
  assertEqual(["0.0.5", "0.1.0"].map((v) => satisfies(v, "~0.0.5")), [true, false]);
}

export function testSetsAndAlternatives(): void {
  const r = ">=1.2.0 <1.5.0 || >=2.0.0 <2.1.0 || 3.0.0";
  assertEqual(["1.1.9", "1.2.0", "1.4.9", "1.5.0", "2.0.5", "2.1.0", "3.0.0", "3.0.1"].map((v) => satisfies(v, r)),
    [false, true, true, false, true, false, true, false]);
  assertEqual(satisfies("1.3.0", "  >1.0.0    <2.0.0  "), true);
  assertEqual(satisfies("1.3.0", ">1.0.0 <1.2.0"), false);
}

export function testPrereleaseRule(): void {
  assertEqual(satisfies("1.3.0-beta", "^1.2.0"), false);
  assertEqual(satisfies("1.2.0-beta.3", ">=1.2.0-beta.1"), true);
  assertEqual(satisfies("1.2.0-beta.3", ">=1.2.0-beta.1 <1.3.0"), true);
  assertEqual(satisfies("1.2.1-beta.3", ">=1.2.0-beta.1"), false);
  assertEqual(satisfies("1.2.0-alpha", ">=1.2.0-beta.1"), false);
  assertEqual(satisfies("2.0.0-rc.1", "^1.0.0"), false);
  assertEqual(satisfies("2.0.0-rc.1", "^1.0.0 || >=2.0.0-rc.0"), true);
  assertEqual(satisfies("1.0.0-rc.1", "*"), false);
  assertEqual(satisfies("1.2.3-beta.5", "^1.2.3-beta.2"), true);
  assertEqual(satisfies("1.2.4-beta.5", "^1.2.3-beta.2"), false);
  assertEqual(satisfies("1.2.4", "^1.2.3-beta.2"), true);
}

export function testInvalidRanges(): void {
  for (const bad of ["", "   ", "^1.2", ">= 1.2.3", "1.2.3 ||", "|| 1.2.3", "1.x", "!1.2.3", "~>1.2.3", "^^1.2.3", "1.2.3 - 2.0.0"]) {
    assertThrows(() => { satisfies("1.2.3", bad); }, `invalid range: ${bad}`);
  }
  assertThrows(() => { satisfies("1.2", "*"); }, "invalid version: 1.2");
}

export function testMaxSatisfying(): void {
  const vs = ["1.0.0", "1.2.0", "1.2.5", "1.10.0", "2.0.0-beta.1", "2.0.0", "2.1.0-rc.1"];
  assertEqual(maxSatisfying(vs, "^1.0.0"), "1.10.0");
  assertEqual(maxSatisfying(vs, "~1.2.0"), "1.2.5");
  assertEqual(maxSatisfying(vs, "*"), "2.0.0");
  assertEqual(maxSatisfying(vs, ">=2.1.0-rc.0"), "2.1.0-rc.1");
  assertEqual(maxSatisfying(vs, "^3.0.0"), null);
  assertEqual(maxSatisfying([], "*"), null);
}

export function testMaxSatisfyingKeepsTextAndFirstOfEquals(): void {
  assertEqual(maxSatisfying(["1.0.0", "v1.4.0+b1", "1.4.0+b2", "1.3.9"], "^1.0.0"), "v1.4.0+b1");
  assertThrows(() => { maxSatisfying(["1.0.0", "bogus"], "*"); }, "invalid version: bogus");
}
