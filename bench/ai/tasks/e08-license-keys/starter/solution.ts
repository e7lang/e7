// Offline license keys for the desktop edition, e.g. "AEBA-GBAF-AYDQ-QCIK". The key is base32 of a
// small binary payload followed by a keyed FNV-1a checksum. The checksum only catches typos and casual
// edits; the activation server does the real check.
//
// Payload (big-endian), 10 bytes:
//   u16 product id
//   u16 expiry day (days since 2020-01-01)
//   u8  seats
//   u8  reserved (0)
//   u32 checksum = FNV-1a 32 over UTF-8(secret) followed by the 6 bytes above
import { base32Decode, base32Encode, concatBytes, utf8Encode } from "e7:encoding";

export interface LicenseInfo {
  product: number;
  expiresDay: number;
  seats: number;
}

const ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
const PAYLOAD_SIZE = 10;
const KEY_CHARS = 16;

function fnv1a(data: Uint8Array): u32 {
  let h: u32 = 0x811c9dc5;
  for (const b of data) {
    h ^= u32(b);
    h *= 16777619;
  }
  return h;
}

function checksum(secret: string, fields: Uint8Array): u32 {
  return fnv1a(concatBytes(utf8Encode(secret), fields));
}

function checkRange(name: string, value: number, max: number): void {
  if (!Number.isInteger(value) || value < 0 || value > max) throw new Error(`bad ${name}`);
}

function group(text: string): string {
  const parts: string[] = [];
  for (let i = 0; i < text.length; i += 4) parts.push(text.slice(i, i + 4));
  return parts.join("-");
}

export function makeKey(info: LicenseInfo, secret: string): string {
  checkRange("product", info.product, 0xffff);
  checkRange("expiry", info.expiresDay, 0xffff);
  checkRange("seats", info.seats, 0xff);
  const bytes = new Uint8Array(PAYLOAD_SIZE);
  const view = new DataView(bytes.buffer);
  view.setUint16(0, info.product);
  view.setUint16(2, info.expiresDay);
  view.setUint8(4, info.seats);
  view.setUint32(6, Number(checksum(secret, bytes.subarray(0, 6))));
  return group(base32Encode(bytes));
}

// Removes dashes and spaces and uppercases; null when the rest is not base32 of the right length.
function normalize(key: string): string | null {
  const text = key.replaceAll("-", "").replaceAll(" ", "").toUpperCase();
  if (text.length !== KEY_CHARS) return null;
  for (const ch of text) if (!ALPHABET.includes(ch)) return null;
  return text;
}

export function parseKey(key: string, secret: string): LicenseInfo | null {
  const text = normalize(key);
  if (text === null) return null;
  const bytes = base32Decode(text);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (u32(view.getUint32(6)) !== checksum(secret, bytes.subarray(0, 6))) return null;
  return { product: view.getUint16(0), expiresDay: view.getUint16(2), seats: view.getUint8(4) };
}

export function dayNumber(isoDate: string): number {
  return Math.floor((Date.parse(isoDate) - Date.UTC(2020, 0, 1)) / 86400000);
}

// "valid", "malformed", "bad checksum", "wrong product" or "expired" (the expiry day itself is valid).
export function checkKey(key: string, secret: string, product: number, today: number): string {
  if (normalize(key) === null) return "malformed";
  const info = parseKey(key, secret);
  if (info === null) return "bad checksum";
  if (info.product !== product) return "wrong product";
  if (today > info.expiresDay) return "expired";
  return "valid";
}
