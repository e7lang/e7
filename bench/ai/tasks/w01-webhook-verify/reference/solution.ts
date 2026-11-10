import { hmac, timingSafeEqual } from "e7:crypto";
import { hexDecode, hexEncode, utf8Encode } from "e7:encoding";

export interface VerifyResult { ok: boolean; reason: string }

function fail(reason: string): VerifyResult {
  return { ok: false, reason };
}

export function verifyWebhook(header: string, body: string, secretHex: string, nowSeconds: number): VerifyResult {
  let t: string | null = null;
  const sigs: string[] = [];
  for (const raw of header.split(",")) {
    const pair = raw.trim();
    const eq = pair.indexOf("=");
    if (eq < 0) return fail("malformed header");
    const key = pair.slice(0, eq).trim();
    const value = pair.slice(eq + 1).trim();
    if (key === "t") t = value;
    else if (key === "v1") sigs.push(value);
  }
  if (t === null || t.length === 0) return fail("missing timestamp");
  for (let i = 0; i < t.length; i++) {
    const c = t.charCodeAt(i);
    if (c < 48 || c > 57) return fail("missing timestamp");
  }
  const ts = Number(t);
  if (Math.abs(nowSeconds - ts) > 300) return fail("stale");
  if (sigs.length === 0) return fail("no signature");
  const expected = utf8Encode(hexEncode(hmac("sha256", hexDecode(secretHex), utf8Encode(`${t}.${body}`))));
  let ok = false;
  for (const s of sigs) {
    if (timingSafeEqual(utf8Encode(s), expected)) ok = true;
  }
  return ok ? { ok: true, reason: "" } : fail("signature mismatch");
}
