// Request signing for the partner API. The backend rebuilds the same canonical string from the
// request it receives and compares the HMAC, so every byte of the format below matters.
import { hash, hmac, timingSafeEqual } from "e7:crypto";
import { hexDecode, hexEncode, utf8Encode } from "e7:encoding";

export interface Header {
  name: string;
  value: string;
}

export interface ApiRequest {
  method: string;
  path: string;
  query: string; // without the leading "?", may be ""
  headers: Header[];
  body: string;
}

export type SignatureAlg = "sha256" | "sha512";

// Headers that take part in the signature (lowercase). Other headers are not signed.
const SIGNED_HEADERS = new Set<string>(["content-type", "host", "x-request-id"]);

const MAX_SKEW_SECONDS = 300;

interface QueryParam {
  key: string;
  value: string;
}

// "b=2&a=1&&c" -> "a=1&b=2&c=": empty parts dropped, sorted by key (stable for repeated keys).
export function canonicalQuery(query: string): string {
  const params: QueryParam[] = [];
  for (const part of query.split("&")) {
    if (part === "") continue;
    const eq = part.indexOf("=");
    if (eq < 0) params.push({ key: part, value: "" });
    else params.push({ key: part.slice(0, eq), value: part.slice(eq + 1) });
  }
  params.sort((a, b) => (a.key < b.key ? -1 : a.key > b.key ? 1 : 0));
  return params.map((p) => `${p.key}=${p.value}`).join("&");
}

// Signed headers present in the request, names lowercased and sorted; repeated headers are
// joined with "," in the order they appear.
function signedHeaders(req: ApiRequest): Header[] {
  const byName = new Map<string, string>();
  for (const h of req.headers) {
    const name = h.name.trim().toLowerCase();
    if (!SIGNED_HEADERS.has(name)) continue;
    const value = h.value.trim();
    const prev = byName.get(name);
    byName.set(name, prev === undefined ? value : `${prev},${value}`);
  }
  const names = [...byName.keys()].sort();
  return names.map((name) => ({ name, value: byName.get(name) ?? "" }));
}

function schemeOf(alg: SignatureAlg): string {
  switch (alg) {
    case "sha256":
      return "E7-HMAC-SHA256";
    case "sha512":
      return "E7-HMAC-SHA512";
  }
}

function algOfScheme(scheme: string): SignatureAlg | null {
  if (scheme === "E7-HMAC-SHA256") return "sha256";
  if (scheme === "E7-HMAC-SHA512") return "sha512";
  return null;
}

export function canonicalRequest(req: ApiRequest, timestamp: number, alg: SignatureAlg = "sha256"): string {
  const lines: string[] = [req.method.toUpperCase(), req.path === "" ? "/" : req.path, canonicalQuery(req.query)];
  for (const h of signedHeaders(req)) lines.push(`${h.name}:${h.value}`);
  lines.push(`x-ts:${timestamp}`);
  lines.push(hexEncode(hash(alg, utf8Encode(req.body))));
  return lines.join("\n");
}

function signature(req: ApiRequest, secretHex: string, timestamp: number, alg: SignatureAlg): string {
  return hexEncode(hmac(alg, hexDecode(secretHex), utf8Encode(canonicalRequest(req, timestamp, alg))));
}

// The value of the Authorization header:
// "E7-HMAC-SHA256 keyId=<id>,ts=<timestamp>,headers=<name;name>,sig=<hex>"
export function signRequest(
  req: ApiRequest,
  keyId: string,
  secretHex: string,
  timestamp: number,
  alg: SignatureAlg = "sha256",
): string {
  if (!Number.isInteger(timestamp) || timestamp < 0) throw new Error("bad timestamp");
  if (keyId === "" || keyId.includes(",") || keyId.includes("=")) throw new Error("bad key id");
  const sig = signature(req, secretHex, timestamp, alg);
  const names = signedHeaders(req).map((h) => h.name).join(";");
  return `${schemeOf(alg)} keyId=${keyId},ts=${timestamp},headers=${names},sig=${sig}`;
}

function isDigits(s: string): boolean {
  if (s.length === 0) return false;
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    if (c < 48 || c > 57) return false;
  }
  return true;
}

// Checks an Authorization header produced by signRequest. Returns "" when it is valid, otherwise
// the first problem found: "malformed", "unknown algorithm", "unknown key", "stale", "bad signature".
export function verifyRequest(authorization: string, req: ApiRequest, keys: Map<string, string>, nowSeconds: number): string {
  const sp = authorization.indexOf(" ");
  if (sp < 0) return "malformed";
  const alg = algOfScheme(authorization.slice(0, sp));
  if (alg === null) return "unknown algorithm";
  let keyId: string | null = null;
  let ts: string | null = null;
  let sig: string | null = null;
  for (const part of authorization.slice(sp + 1).split(",")) {
    const eq = part.indexOf("=");
    if (eq < 0) return "malformed";
    const key = part.slice(0, eq);
    const value = part.slice(eq + 1);
    if (key === "keyId") keyId = value;
    else if (key === "ts") ts = value;
    else if (key === "sig") sig = value;
  }
  if (keyId === null || ts === null || sig === null || !isDigits(ts)) return "malformed";
  const secretHex = keys.get(keyId);
  if (secretHex === undefined) return "unknown key";
  const timestamp = Number(ts);
  if (Math.abs(nowSeconds - timestamp) > MAX_SKEW_SECONDS) return "stale";
  const expected = signature(req, secretHex, timestamp, alg);
  return timingSafeEqual(utf8Encode(sig), utf8Encode(expected)) ? "" : "bad signature";
}
