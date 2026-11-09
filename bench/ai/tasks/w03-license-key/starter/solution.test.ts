import { assertEqual } from "e7:test";
import { checkLicenseKey, makeLicenseKey } from "./solution.ts";

export function testValidKey(): void {
  assertEqual(checkLicenseKey("PABCD-2345E-FGHJ6-789KQ"), {
    ok: true, error: "", normalized: "PABCD-2345E-FGHJ6-789KQ", edition: "pro",
  });
}

export function testMake(): void {
  assertEqual(makeLicenseKey("PABCD2345EFGHJ6789K"), "PABCD-2345E-FGHJ6-789KQ");
}

export function testBadChecksum(): void {
  assertEqual(checkLicenseKey("PABCD-2345E-FGHJ6-789KR").error, "bad checksum");
}
