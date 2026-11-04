// API request signing: canonical query string + timestamp + HMAC-SHA256 (hex), as used by many
// open platforms. The secret can be a host-held KeyHandle in production (see docs/EMBEDDING.md).
import { hmac, sha256 } from "e7:crypto";
import { hexEncode } from "e7:encoding";

export interface SignedRequest { query: string; signature: string; contentSha256: string }

export function canonicalQuery(params: Map<string, string>): string {
  const keys = [...params.keys()].sort();
  return keys.map(k => `${encodeURIComponent(k)}=${encodeURIComponent(params.get(k)!)}`).join("&");
}

export function signRequest(method: string, path: string, params: Map<string, string>, body: string,
                            secret: Uint8Array, timestamp: number, nonce: string): SignedRequest {
  const enc = new TextEncoder();
  const all = new Map<string, string>(params);
  all.set("timestamp", String(timestamp));
  all.set("nonce", nonce);
  const query = canonicalQuery(all);
  const contentSha256 = hexEncode(sha256(enc.encode(body)));
  const stringToSign = [method.toUpperCase(), path, query, contentSha256].join("\n");
  const signature = hexEncode(hmac("sha256", secret, enc.encode(stringToSign)));
  return { query, signature, contentSha256 };
}

const params = new Map<string, string>([["amount", "100.00"], ["currency", "CNY"], ["memo", "订单 #42"]]);
const r = signRequest("post", "/v1/pay", params, '{"item":"book"}', new TextEncoder().encode("s3cr3t"), 1700000000, "n-001");
console.log(r.query);
console.log(r.contentSha256);
console.log(r.signature);
