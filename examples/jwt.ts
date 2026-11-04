// JSON Web Tokens: HS256 sign/verify and ES256 verify (RFC 7519 / RFC 7515).
import { hmac, ecdsaSign, ecdsaVerify, ecdsaPublicKey, sha256, timingSafeEqual } from "e7:crypto";
import { base64UrlEncode, base64UrlDecode, utf8Encode, utf8Decode } from "e7:encoding";

interface Header { alg: string; typ: string }
interface Claims { sub: string; iat: number; exp: number; role: string }

function b64json(s: string): string { return base64UrlEncode(utf8Encode(s)); }

export function signHS256(claims: Claims, key: Uint8Array): string {
  const head = b64json(JSON.stringify({ alg: "HS256", typ: "JWT" }));
  const body = b64json(JSON.stringify(claims));
  const sig = hmac("sha256", key, utf8Encode(`${head}.${body}`));
  return `${head}.${body}.${base64UrlEncode(sig)}`;
}

export function verify(token: string, key: Uint8Array, ecPublicKey: Uint8Array | null, now: number): Claims | null {
  const parts = token.split(".");
  if (parts.length !== 3) return null;
  const header = JSON.parse(utf8Decode(base64UrlDecode(parts[0]))) as Header;
  const signingInput = utf8Encode(`${parts[0]}.${parts[1]}`);
  const sig = base64UrlDecode(parts[2]);
  let ok = false;
  if (header.alg === "HS256") ok = timingSafeEqual(hmac("sha256", key, signingInput), sig);
  else if (header.alg === "ES256" && ecPublicKey !== null) ok = ecdsaVerify("P-256", "sha256", ecPublicKey, signingInput, sig);
  if (!ok) return null;
  const claims = JSON.parse(utf8Decode(base64UrlDecode(parts[1]))) as Claims;
  return claims.exp > now ? claims : null;
}

const key = utf8Encode("your-256-bit-secret");
const claims: Claims = { sub: "user-42", iat: 1700000000, exp: 1700003600, role: "admin" };
const token = signHS256(claims, key);
console.log(token);
console.log(verify(token, key, null, 1700000100)?.role ?? "invalid");
console.log(verify(token, key, null, 1800000000)?.role ?? "expired");
console.log(verify(token.slice(0, -2) + "xx", key, null, 1700000100)?.role ?? "bad signature");
// ES256 round trip (deterministic key for the demo)
const priv = sha256(utf8Encode("demo ec key"));
const pub = ecdsaPublicKey("P-256", priv);
const head = b64json(JSON.stringify({ alg: "ES256", typ: "JWT" }));
const body = b64json(JSON.stringify(claims));
const esig = ecdsaSign("P-256", "sha256", priv, utf8Encode(`${head}.${body}`));
const es = `${head}.${body}.${base64UrlEncode(esig)}`;
console.log(verify(es, key, pub, 1700000100)?.sub ?? "invalid");
