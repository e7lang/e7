import { assertEqual } from "e7:test";
import { checkKey, dayNumber, makeKey, parseKey } from "./solution.ts";

const secret = "s3cret";

export function testMakeKey(): void {
  assertEqual(makeKey({ product: 42, expiresDay: dayNumber("2025-12-31"), seats: 5 }, secret), "AAVA-RDYF-ABE2-56QN");
}

export function testParseKey(): void {
  assertEqual(parseKey("aava-rdyf-abe2-56qn", secret), { product: 42, expiresDay: 2191, seats: 5 });
  assertEqual(parseKey("AAVA-RDYF-ABE2-56QN", "other"), null);
}

export function testCheckKey(): void {
  assertEqual(checkKey("AAVA-RDYF-ABE2-56QN", secret, 42, dayNumber("2025-12-31")), "valid");
  assertEqual(checkKey("AAVA-RDYF-ABE2-56QN", secret, 42, dayNumber("2026-01-01")), "expired");
  assertEqual(checkKey("AAVA-RDYF-ABE2-56QN", secret, 43, 0), "wrong product");
  assertEqual(checkKey("AAAA-BBBB-CCCC", secret, 42, 0), "malformed");
}

export function testTypoIsBadChecksum(): void {
  assertEqual(checkKey("AAVA-RDYF-ABE2-56QM", secret, 42, 0), "bad checksum");
}
