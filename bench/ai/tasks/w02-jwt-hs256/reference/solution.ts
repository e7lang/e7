import { hmac, timingSafeEqual } from "e7:crypto";
import { base64UrlDecode, base64UrlEncode, utf8Decode, utf8Encode } from "e7:encoding";
import { tryParse } from "e7:json";

export interface JwtResult { valid: boolean; error: string; claimsJson: string }

interface Header { alg: string }
interface Claims { exp?: number; nbf?: number }

const HEADER = '{"alg":"HS256","typ":"JWT"}';

function b64(text: string): string {
  return base64UrlEncode(utf8Encode(text));
}

function mac(input: string, secret: string): string {
  return base64UrlEncode(hmac("sha256", utf8Encode(secret), utf8Encode(input)));
}

function isBase64Url(part: string): boolean {
  if (part.length === 0 || part.length % 4 === 1) return false;
  for (let i = 0; i < part.length; i++) {
    const c = part.charCodeAt(i);
    const ok = (c >= 65 && c <= 90) || (c >= 97 && c <= 122) || (c >= 48 && c <= 57) || c === 45 || c === 95;
    if (!ok) return false;
  }
  return true;
}

function fail(error: string): JwtResult {
  return { valid: false, error, claimsJson: "" };
}

export function signJwt(claimsJson: string, secret: string): string {
  const input = `${b64(HEADER)}.${b64(claimsJson)}`;
  return `${input}.${mac(input, secret)}`;
}

export function verifyJwt(token: string, secret: string, nowSeconds: number): JwtResult {
  const parts = token.split(".");
  if (parts.length !== 3 || !parts.every((p) => isBase64Url(p))) return fail("malformed token");
  const header = tryParse(utf8Decode(base64UrlDecode(parts[0]))) as Header | null;
  const claimsJson = utf8Decode(base64UrlDecode(parts[1]));
  const claims = tryParse(claimsJson) as Claims | null;
  if (header === null || claims === null) return fail("malformed token");
  if (header.alg !== "HS256") return fail("unsupported algorithm");
  const expected = utf8Encode(mac(`${parts[0]}.${parts[1]}`, secret));
  if (!timingSafeEqual(utf8Encode(parts[2]), expected)) return fail("invalid signature");
  if (claims.exp !== undefined && nowSeconds >= claims.exp) return fail("expired");
  if (claims.nbf !== undefined && nowSeconds < claims.nbf) return fail("not yet valid");
  return { valid: true, error: "", claimsJson };
}
