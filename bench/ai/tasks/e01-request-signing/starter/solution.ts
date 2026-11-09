// Request signing for the partner API. The backend rebuilds the same canonical string from the
// request it receives and compares the HMAC, so every byte of the format below matters.
import { hmac, sha256 } from "e7:crypto";
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

// Headers that take part in the signature (lowercase). Other headers are not signed.
const SIGNED_HEADERS = new Set<string>(["content-type", "host", "x-request-id"]);

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

export function canonicalRequest(req: ApiRequest, timestamp: number): string {
  const lines: string[] = [req.method.toUpperCase(), req.path === "" ? "/" : req.path, canonicalQuery(req.query)];
  for (const h of signedHeaders(req)) lines.push(`${h.name}:${h.value}`);
  lines.push(`x-ts:${timestamp}`);
  lines.push(hexEncode(sha256(utf8Encode(req.body))));
  return lines.join("\n");
}

// The value of the Authorization header:
// "E7-HMAC-SHA256 keyId=<id>,ts=<timestamp>,headers=<name;name>,sig=<hex>"
export function signRequest(req: ApiRequest, keyId: string, secretHex: string, timestamp: number): string {
  if (!Number.isInteger(timestamp) || timestamp < 0) throw new Error("bad timestamp");
  if (keyId === "" || keyId.includes(",") || keyId.includes("=")) throw new Error("bad key id");
  const canonical = canonicalRequest(req, timestamp);
  const sig = hexEncode(hmac("sha256", hexDecode(secretHex), utf8Encode(canonical)));
  const names = signedHeaders(req).map((h) => h.name).join(";");
  return `E7-HMAC-SHA256 keyId=${keyId},ts=${timestamp},headers=${names},sig=${sig}`;
}
