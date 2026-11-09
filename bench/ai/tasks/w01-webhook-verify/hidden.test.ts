import { assertEqual } from "e7:test";
import { hmac } from "e7:crypto";
import { hexEncode, hexDecode, utf8Encode } from "e7:encoding";
import { verifyWebhook } from "./solution.ts";

const secret = "00112233445566778899aabbccddeeff";
const other = "ffeeddccbbaa99887766554433221100";
const now = 1700000000;

function sign(key: string, t: number, body: string): string {
  return hexEncode(hmac("sha256", hexDecode(key), utf8Encode(`${t}.${body}`)));
}

export function testValidSingle(): void {
  const b = '{"id":42}';
  assertEqual(verifyWebhook(`t=${now},v1=${sign(secret, now, b)}`, b, secret, now), { ok: true, reason: "" });
}

export function testRotationSecondMatches(): void {
  const b = "hello";
  const h = `t=${now},v1=${sign(other, now, b)},v1=${sign(secret, now, b)}`;
  assertEqual(verifyWebhook(h, b, secret, now + 5), { ok: true, reason: "" });
}

export function testOtherKeysAndSpacesIgnored(): void {
  const b = "body";
  const h = ` t=${now} , v2=abcd, v1=${sign(secret, now, b)} `;
  assertEqual(verifyWebhook(h, b, secret, now), { ok: true, reason: "" });
}

export function testOrderDoesNotMatter(): void {
  const b = "body";
  assertEqual(verifyWebhook(`v1=${sign(secret, now, b)},t=${now}`, b, secret, now).ok, true);
}

export function testMismatch(): void {
  assertEqual(verifyWebhook(`t=${now},v1=${sign(other, now, "b")}`, "b", secret, now), { ok: false, reason: "signature mismatch" });
}

export function testBodyTampered(): void {
  assertEqual(verifyWebhook(`t=${now},v1=${sign(secret, now, "a")}`, "b", secret, now).reason, "signature mismatch");
}

export function testTimestampIsSigned(): void {
  // a valid signature for another timestamp must not verify
  assertEqual(verifyWebhook(`t=${now + 1},v1=${sign(secret, now, "b")}`, "b", secret, now).reason, "signature mismatch");
}

export function testStaleBothDirections(): void {
  const b = "b";
  const h = `t=${now},v1=${sign(secret, now, b)}`;
  assertEqual(verifyWebhook(h, b, secret, now + 300).ok, true);
  assertEqual(verifyWebhook(h, b, secret, now - 300).ok, true);
  assertEqual(verifyWebhook(h, b, secret, now + 301).reason, "stale");
  assertEqual(verifyWebhook(h, b, secret, now - 301).reason, "stale");
}

export function testMissingTimestamp(): void {
  assertEqual(verifyWebhook(`v1=${sign(secret, now, "b")}`, "b", secret, now).reason, "missing timestamp");
  assertEqual(verifyWebhook(`t=abc,v1=00`, "b", secret, now).reason, "missing timestamp");
  assertEqual(verifyWebhook(`t=1.5,v1=00`, "b", secret, now).reason, "missing timestamp");
  assertEqual(verifyWebhook(`t=,v1=00`, "b", secret, now).reason, "missing timestamp");
}

export function testNoSignature(): void {
  assertEqual(verifyWebhook(`t=${now}`, "b", secret, now).reason, "no signature");
  assertEqual(verifyWebhook(`t=${now},v2=abcd`, "b", secret, now).reason, "no signature");
}

export function testMalformed(): void {
  assertEqual(verifyWebhook(`t=${now},garbage`, "b", secret, now).reason, "malformed header");
  assertEqual(verifyWebhook(``, "b", secret, now).reason, "malformed header");
}

export function testCheckOrder(): void {
  // stale is reported before a missing signature
  assertEqual(verifyWebhook(`t=${now - 1000}`, "b", secret, now).reason, "stale");
}

export function testNonHexSignatureIsMismatch(): void {
  assertEqual(verifyWebhook(`t=${now},v1=zz`, "b", secret, now).reason, "signature mismatch");
}

export function testUnicodeBody(): void {
  const b = '{"name":"Zoë 🚀"}';
  assertEqual(verifyWebhook(`t=${now},v1=${sign(secret, now, b)}`, b, secret, now).ok, true);
}
