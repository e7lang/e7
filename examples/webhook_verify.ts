// Verify webhook signatures in the "t=<ts>,v1=<hex>" header format with tolerance window.
import { hmac, timingSafeEqual } from "e7:crypto";
import { hexDecode, hexEncode, utf8Encode } from "e7:encoding";

export function verifyWebhook(payload: string, header: string, secret: string, now: number, tolerance: number): boolean {
  let ts = -1;
  const sigs: string[] = [];
  for (const part of header.split(",")) {
    const eq = part.indexOf("=");
    if (eq < 0) continue;
    const k = part.slice(0, eq).trim(), v = part.slice(eq + 1).trim();
    if (k === "t") ts = parseInt(v, 10);
    else if (k === "v1") sigs.push(v);
  }
  if (ts < 0 || Math.abs(now - ts) > tolerance) return false;
  const expected = hmac("sha256", utf8Encode(secret), utf8Encode(`${ts}.${payload}`));
  return sigs.some(s => s.length === 64 && timingSafeEqual(hexDecode(s), expected));
}

const secret = "whsec_test";
const body = '{"event":"paid","id":"evt_1"}';
const t = 1700000000;
const good = hexEncode(hmac("sha256", utf8Encode(secret), utf8Encode(`${t}.${body}`)));
console.log(verifyWebhook(body, `t=${t},v1=${good}`, secret, t + 30, 300));
console.log(verifyWebhook(body, `t=${t},v1=${good}`, secret, t + 3000, 300));
console.log(verifyWebhook(body + " ", `t=${t},v1=${good}`, secret, t + 30, 300));
console.log(verifyWebhook(body, `t=${t},v1=deadbeef,v1=${good}`, secret, t, 300));
