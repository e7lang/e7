import { assertEqual } from "e7:test";
import { hmac } from "e7:crypto";
import { base64UrlEncode, utf8Encode } from "e7:encoding";
import { signJwt, verifyJwt } from "./solution.ts";

const secret = "k3y-for-tests";
const now = 1700000000;

function b64(s: string): string {
  return base64UrlEncode(utf8Encode(s));
}

// a token with an arbitrary header, signed correctly
function craft(header: string, claims: string, key: string): string {
  const input = `${b64(header)}.${b64(claims)}`;
  return `${input}.${base64UrlEncode(hmac("sha256", utf8Encode(key), utf8Encode(input)))}`;
}

function err(token: string, at: number): string {
  return verifyJwt(token, secret, at).error;
}

export function testJwtIoVector(): void {
  const t = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ.SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c";
  assertEqual(verifyJwt(t, "your-256-bit-secret", now), {
    valid: true, error: "", claimsJson: '{"sub":"1234567890","name":"John Doe","iat":1516239022}',
  });
  assertEqual(verifyJwt(t, "wrong", now).error, "invalid signature");
}

export function testSignMatchesManualConstruction(): void {
  const claims = '{ "sub": "a",  "role":"admin" }';
  assertEqual(signJwt(claims, secret), craft('{"alg":"HS256","typ":"JWT"}', claims, secret));
}

export function testUnicodeClaimsRoundTrip(): void {
  const claims = '{"name":"Zoë 🚀","n":1}';
  const r = verifyJwt(signJwt(claims, "sécret"), "sécret", now);
  assertEqual(r, { valid: true, error: "", claimsJson: claims });
}

export function testExpBoundary(): void {
  const t = signJwt(`{"exp":${now + 10}}`, secret);
  assertEqual(err(t, now + 9), "");
  assertEqual(err(t, now + 10), "expired");
  assertEqual(err(t, now + 1000), "expired");
}

export function testNbfBoundary(): void {
  const t = signJwt(`{"nbf":${now}}`, secret);
  assertEqual(err(t, now - 1), "not yet valid");
  assertEqual(err(t, now), "");
}

export function testExpCheckedBeforeNbf(): void {
  const t = signJwt(`{"nbf":${now + 100},"exp":${now - 100}}`, secret);
  assertEqual(err(t, now), "expired");
}

export function testTamperedPayload(): void {
  const parts = signJwt('{"role":"user"}', secret).split(".");
  const forged = `${parts[0]}.${b64('{"role":"admin"}')}.${parts[2]}`;
  assertEqual(verifyJwt(forged, secret, now), { valid: false, error: "invalid signature", claimsJson: "" });
}

export function testSignatureCheckedBeforeTime(): void {
  const t = signJwt(`{"exp":${now - 5}}`, "other");
  assertEqual(err(t, now), "invalid signature");
}

export function testUnsupportedAlgorithm(): void {
  assertEqual(err(craft('{"alg":"none","typ":"JWT"}', "{}", secret), now), "unsupported algorithm");
  assertEqual(err(craft('{"alg":"HS512"}', "{}", secret), now), "unsupported algorithm");
  // alg is checked before the signature
  assertEqual(err(`${b64('{"alg":"none"}')}.${b64("{}")}.AAAA`, now), "unsupported algorithm");
}

export function testHeaderWithoutTypIsFine(): void {
  assertEqual(err(craft('{"alg":"HS256"}', '{"sub":"x"}', secret), now), "");
}

export function testMalformedStructure(): void {
  const good = signJwt("{}", secret);
  assertEqual(err("abc", now), "malformed token");
  assertEqual(err("a.b", now), "malformed token");
  assertEqual(err(`${good}.extra`, now), "malformed token");
  assertEqual(err("", now), "malformed token");
  const p = good.split(".");
  assertEqual(err(`${p[0]}.${p[1]}.`, now), "malformed token");
  assertEqual(err(`.${p[1]}.${p[2]}`, now), "malformed token");
}

export function testMalformedBase64(): void {
  const p = signJwt("{}", secret).split(".");
  assertEqual(err(`${p[0]}.${p[1]}!.${p[2]}`, now), "malformed token");
  assertEqual(err(`${p[0]}.${p[1]}.${p[2]}+/`, now), "malformed token");
  assertEqual(err(`${p[0]}=.${p[1]}.${p[2]}`, now), "malformed token");
}

export function testMalformedJson(): void {
  assertEqual(err(craft("not json", "{}", secret), now), "malformed token");
  assertEqual(err(craft('{"typ":"JWT"}', "{}", secret), now), "malformed token");
  assertEqual(err(craft('{"alg":"HS256"}', "{oops", secret), now), "malformed token");
  assertEqual(err(craft('{"alg":"HS256"}', '{"exp":"soon"}', secret), now), "malformed token");
}

export function testClaimsJsonIsExactPayloadText(): void {
  const claims = '{\n  "sub": "pretty"\n}';
  assertEqual(verifyJwt(signJwt(claims, secret), secret, now).claimsJson, claims);
}
