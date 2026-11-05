import { assertEqual } from "e7:test";
import { hmac } from "e7:crypto";
import { hexEncode, utf8Encode } from "e7:encoding";
import { verify } from "./webhook_verify.ts";

function header(secret: string, t: number, body: string): string {
  return `t=${t},v1=${hexEncode(hmac("sha256", utf8Encode(secret), utf8Encode(`${t}.${body}`)))}`;
}

export function testValidSignature(): void {
  assertEqual(verify("whsec", header("whsec", 1000, "{}"), "{}", 1010, 300), '{"ok":true,"reason":""}');
}

export function testWrongSecret(): void {
  assertEqual(verify("whsec", header("other", 1000, "{}"), "{}", 1010, 300), '{"ok":false,"reason":"signature mismatch"}');
}

export function testExpired(): void {
  assertEqual(verify("whsec", header("whsec", 1000, "{}"), "{}", 5000, 300), '{"ok":false,"reason":"timestamp outside tolerance"}');
}

export function testMalformed(): void {
  assertEqual(verify("whsec", "garbage", "{}", 0, 300), '{"ok":false,"reason":"malformed header"}');
}
