import { assertEqual, assertThrows } from "e7:test";
import { checkLicenseKey, makeLicenseKey } from "./solution.ts";

function fail(error: string): { ok: boolean; error: string; normalized: string; edition: string } {
  return { ok: false, error, normalized: "", edition: "" };
}

export function testEditions(): void {
  assertEqual(checkLicenseKey("S2222-22222-22222-2222S").edition, "standard");
  assertEqual(checkLicenseKey("EZZZZ-ZZZZZ-ZZZZZ-ZZZZH").edition, "enterprise");
  assertEqual(checkLicenseKey("P9GXK-2MW7Q-4TZR8-HN3VE").edition, "pro");
}

export function testNormalization(): void {
  const want = { ok: true, error: "", normalized: "P9GXK-2MW7Q-4TZR8-HN3VE", edition: "pro" };
  assertEqual(checkLicenseKey("p9gxk-2mw7q-4tzr8-hn3ve"), want);
  assertEqual(checkLicenseKey("P9GXK2MW7Q4TZR8HN3VE"), want);
  assertEqual(checkLicenseKey("  P9GXK 2MW7Q 4TZR8 HN3VE "), want);
  assertEqual(checkLicenseKey("P9-GXK2M-W7Q4TZ--R8HN3VE"), want);
}

export function testWrongLength(): void {
  assertEqual(checkLicenseKey(""), fail("wrong length"));
  assertEqual(checkLicenseKey("P9GXK-2MW7Q-4TZR8-HN3V"), fail("wrong length"));
  assertEqual(checkLicenseKey("P9GXK-2MW7Q-4TZR8-HN3VEE"), fail("wrong length"));
}

export function testInvalidCharacter(): void {
  assertEqual(checkLicenseKey("P9GXK-2OW7Q-4TZR8-HN3VE"), fail('invalid character "O" at position 7'));
  assertEqual(checkLicenseKey("p9gxk-2mw7q-4tzr8-hn3vi"), fail('invalid character "I" at position 20'));
  assertEqual(checkLicenseKey("09GXK-2MW7Q-4TZR8-HN3V1"), fail('invalid character "0" at position 1'));
  assertEqual(checkLicenseKey("P9GXK_2MW7Q_4TZR8HN3"), fail('invalid character "_" at position 6'));
}

export function testLengthCheckedFirst(): void {
  assertEqual(checkLicenseKey("O1O1"), fail("wrong length"));
}

export function testBadChecksum(): void {
  assertEqual(checkLicenseKey("P9GXK-2MW7Q-4TZR8-HN3VF"), fail("bad checksum"));
  // two characters swapped
  assertEqual(checkLicenseKey("P9GXK-2MW7Q-4TZR8-H3NVE"), fail("bad checksum"));
}

export function testUnknownEditionAfterChecksum(): void {
  assertEqual(checkLicenseKey("XQ7M4-KD9W2-HTRNP-V3B8T"), fail("unknown edition"));
  assertEqual(checkLicenseKey("XQ7M4-KD9W2-HTRNP-V3B8U"), fail("bad checksum"));
}

export function testMakeThenCheck(): void {
  const bodies = ["S222222222222222222", "EZZZZZZZZZZZZZZZZZZ", "PXYZW23456789ABCDEF", "SQRSTUVWXYZ23456789"];
  for (const b of bodies) {
    const key = makeLicenseKey(b);
    const r = checkLicenseKey(key);
    assertEqual(r.ok, true, key);
    assertEqual(r.normalized, key);
    assertEqual(key.replaceAll("-", "").slice(0, 19), b);
  }
  assertEqual(makeLicenseKey("S222222222222222222"), "S2222-22222-22222-2222S");
  assertEqual(makeLicenseKey("XQ7M4KD9W2HTRNPV3B8"), "XQ7M4-KD9W2-HTRNP-V3B8T");
}

export function testMakeRejectsBadBody(): void {
  assertThrows(() => { makeLicenseKey("PABCD2345EFGHJ6789"); }, "invalid key body");
  assertThrows(() => { makeLicenseKey("PABCD2345EFGHJ6789KK"); }, "invalid key body");
  assertThrows(() => { makeLicenseKey("PABCD2345EFGHJ6789O"); }, "invalid key body");
  assertThrows(() => { makeLicenseKey("pabcd2345efghj6789k"); }, "invalid key body");
  assertThrows(() => { makeLicenseKey("PABCD-2345E-FGHJ6-7"); }, "invalid key body");
}
