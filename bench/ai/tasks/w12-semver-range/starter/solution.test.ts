import { assertEqual } from "e7:test";
import { parseVersion, compareVersions, satisfies, maxSatisfying } from "./solution.ts";

export function testParse(): void {
  assertEqual(parseVersion("1.4.0-beta.2+exp.sha.5114f85"), { major: 1, minor: 4, patch: 0, prerelease: ["beta", "2"] });
}

export function testCompare(): void {
  assertEqual(compareVersions("1.0.0-rc.1", "1.0.0"), -1);
  assertEqual(compareVersions("1.10.0", "1.9.9"), 1);
}

export function testRanges(): void {
  assertEqual(satisfies("1.4.2", "^1.2.0"), true);
  assertEqual(satisfies("2.0.0", "^1.2.0"), false);
  assertEqual(maxSatisfying(["1.2.0", "1.9.3", "2.1.0"], "~1.2.0 || ^1.5.0"), "1.9.3");
}
