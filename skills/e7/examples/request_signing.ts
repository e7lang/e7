// API request signing: canonical query + body hash + HMAC-SHA256, as used by many payment APIs.
// The host passes the secret as a KeyHandle so the key never enters script memory; the string
// variant is for tools and tests.
import { hmac, sha256 } from "e7:crypto";
import type { KeyHandle } from "e7:crypto";
import { hexEncode, utf8Encode } from "e7:encoding";

interface Request { method: string; path: string; params: Map<string, string>; body: string; timestamp: number }

export function canonicalQuery(params: Map<string, string>): string {
  const keys = [...params.keys()].sort();
  return keys.map(k => `${encodeURIComponent(k)}=${encodeURIComponent(params.get(k) ?? "")}`).join("&");
}

export function stringToSign(r: Request): string {
  return [r.method.toUpperCase(), r.path, canonicalQuery(r.params), String(r.timestamp), hexEncode(sha256(utf8Encode(r.body)))].join("\n");
}

// Host API: request as JSON (params as an object with string values), key held by the app.
interface RequestJson { method: string; path: string; params: Map<string, string>; body: string; timestamp: number }

export function sign(requestJson: string, key: KeyHandle): string {
  const r = JSON.parse(requestJson) as RequestJson;
  return hexEncode(hmac("sha256", key, utf8Encode(stringToSign(r))));
}

export function signWithSecret(requestJson: string, secret: string): string {
  const r = JSON.parse(requestJson) as RequestJson;
  return hexEncode(hmac("sha256", utf8Encode(secret), utf8Encode(stringToSign(r))));
}

export function main(): string {
  return signWithSecret('{"method":"post","path":"/v1/pay","params":{"currency":"CNY","amount":"100"},"body":"{}","timestamp":1700000000}', "s3cr3t");
}
