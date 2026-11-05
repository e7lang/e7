// Webhook verification: header "t=<unix seconds>,v1=<hex HMAC-SHA256 of '<t>.<body>'>".
// Returns a result object instead of throwing, so the host can report why a request was rejected.
import { hmac, timingSafeEqual } from "e7:crypto";
import { hexDecode, utf8Encode } from "e7:encoding";

interface Verdict { ok: boolean; reason: string }

function parseHeader(header: string): Map<string, string> {
  const fields = new Map<string, string>();
  for (const part of header.split(",")) {
    const eq = part.indexOf("=");
    if (eq > 0) fields.set(part.slice(0, eq).trim(), part.slice(eq + 1).trim());
  }
  return fields;
}

function isHex(s: string): boolean {
  if (s.length === 0 || s.length % 2 !== 0) return false;
  for (const ch of s) if ("0123456789abcdefABCDEF".indexOf(ch) < 0) return false;
  return true;
}

export function verify(secret: string, header: string, body: string, nowSeconds: number, toleranceSeconds: number): string {
  const f = parseHeader(header);
  const t = f.get("t");
  const v1 = f.get("v1");
  let v: Verdict;
  if (t === undefined || v1 === undefined || !isHex(v1)) v = { ok: false, reason: "malformed header" };
  else if (!Number.isInteger(Number(t)) || Math.abs(nowSeconds - Number(t)) > toleranceSeconds) v = { ok: false, reason: "timestamp outside tolerance" };
  else {
    const expected = hmac("sha256", utf8Encode(secret), utf8Encode(`${t}.${body}`));
    v = timingSafeEqual(expected, hexDecode(v1)) ? { ok: true, reason: "" } : { ok: false, reason: "signature mismatch" };
  }
  return JSON.stringify(v);
}

export function main(): string {
  return verify("whsec", "t=1700000000,v1=00", "{}", 1700000010, 300);
}
