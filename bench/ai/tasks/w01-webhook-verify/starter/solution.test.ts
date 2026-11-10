import { assertEqual } from "e7:test";
import { hmac } from "e7:crypto";
import { hexEncode, hexDecode, utf8Encode } from "e7:encoding";
import { verifyWebhook } from "./solution.ts";

const secret = "00112233445566778899aabbccddeeff";

function sign(t: number, body: string): string {
  return hexEncode(hmac("sha256", hexDecode(secret), utf8Encode(`${t}.${body}`)));
}

export function testValid(): void {
  const body = '{"event":"paid"}';
  const r = verifyWebhook(`t=1700000000,v1=${sign(1700000000, body)}`, body, secret, 1700000010);
  assertEqual(r, { ok: true, reason: "" });
}

export function testStale(): void {
  const body = "x";
  const r = verifyWebhook(`t=1700000000,v1=${sign(1700000000, body)}`, body, secret, 1700000400);
  assertEqual(r, { ok: false, reason: "stale" });
}
