// Offline license keys for the desktop edition, e.g. "AEBA-GBAF-AYDQ-QCIK". The key is base32 of a
// small binary payload followed by a keyed FNV-1a checksum. The checksum only catches typos and casual
// edits; the activation server does the real check.
//
// Version 1 payload (big-endian), 10 bytes, 16 key characters:
//   u16 product id
//   u16 expiry day (days since 2020-01-01)
//   u8  seats
//   u8  reserved (0)
//   u32 checksum = FNV-1a 32 over UTF-8(secret) followed by the 6 bytes above
//
// Version 2 payload, 15 bytes, 24 key characters:
//   u8  version (2)
//   u16 product id
//   u16 expiry day
//   u16 seats
//   u32 features (bit n set = feature n enabled)
//   u32 checksum over UTF-8(secret) followed by the 11 bytes above
import { base32Decode, base32Encode, concatBytes, utf8Encode } from "e7:encoding";

export interface LicenseInfo {
  product: number;
  expiresDay: number;
  seats: number;
  features?: number; // version 2 keys only
}

const ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
const V1_CHARS = 16;
const V2_CHARS = 24;
const DAY_MS = 86400000;

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

function makeV1(info: LicenseInfo, secret: string): Uint8Array {
  const bytes = new Uint8Array(10);
  const view = new DataView(bytes.buffer);
  view.setUint16(0, info.product);
  view.setUint16(2, info.expiresDay);
  view.setUint8(4, info.seats);
  view.setUint32(6, Number(checksum(secret, bytes.subarray(0, 6))));
  return bytes;
}

function makeV2(info: LicenseInfo, features: number, secret: string): Uint8Array {
  const bytes = new Uint8Array(15);
  const view = new DataView(bytes.buffer);
  view.setUint8(0, 2);
  view.setUint16(1, info.product);
  view.setUint16(3, info.expiresDay);
  view.setUint16(5, info.seats);
  view.setUint32(7, features);
  view.setUint32(11, Number(checksum(secret, bytes.subarray(0, 11))));
  return bytes;
}

export function makeKey(info: LicenseInfo, secret: string): string {
  checkRange("product", info.product, 0xffff);
  checkRange("expiry", info.expiresDay, 0xffff);
  const v2 = info.features !== undefined || info.seats > 0xff;
  checkRange("seats", info.seats, v2 ? 0xffff : 0xff);
  if (!v2) return group(base32Encode(makeV1(info, secret)));
  const features = info.features ?? 0;
  checkRange("features", features, 0xffffffff);
  return group(base32Encode(makeV2(info, features, secret)));
}

// Removes dashes and spaces and uppercases; null when the rest is not base32 of a known length.
function normalize(key: string): string | null {
  const text = key.replaceAll("-", "").replaceAll(" ", "").toUpperCase();
  if (text.length !== V1_CHARS && text.length !== V2_CHARS) return null;
  for (const ch of text) if (!ALPHABET.includes(ch)) return null;
  return text;
}

export function parseKey(key: string, secret: string): LicenseInfo | null {
  const text = normalize(key);
  if (text === null) return null;
  const bytes = base32Decode(text);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (text.length === V1_CHARS) {
    if (u32(view.getUint32(6)) !== checksum(secret, bytes.subarray(0, 6))) return null;
    return { product: view.getUint16(0), expiresDay: view.getUint16(2), seats: view.getUint8(4) };
  }
  if (view.getUint8(0) !== 2) return null;
  if (u32(view.getUint32(11)) !== checksum(secret, bytes.subarray(0, 11))) return null;
  return { product: view.getUint16(1), expiresDay: view.getUint16(3), seats: view.getUint16(5), features: view.getUint32(7) };
}

export function dayNumber(isoDate: string): number {
  return Math.floor((Date.parse(isoDate) - Date.UTC(2020, 0, 1)) / DAY_MS);
}

function isoDay(day: number): string {
  return new Date(Date.UTC(2020, 0, 1) + day * DAY_MS).toISOString().slice(0, 10);
}

// "valid", "malformed", "unsupported version", "bad checksum", "wrong product" or "expired"
// (the expiry day itself is valid).
export function checkKey(key: string, secret: string, product: number, today: number): string {
  const text = normalize(key);
  if (text === null) return "malformed";
  if (text.length === V2_CHARS && base32Decode(text)[0] !== 2) return "unsupported version";
  const info = parseKey(key, secret);
  if (info === null) return "bad checksum";
  if (info.product !== product) return "wrong product";
  if (today > info.expiresDay) return "expired";
  return "valid";
}

function featureBits(features: number): number[] {
  const bits: number[] = [];
  const f = u32(features);
  for (let bit = 0; bit < 32; bit++) {
    if (((f >> u32(bit)) & 1) === 1) bits.push(bit);
  }
  return bits;
}

export function hasFeature(key: string, secret: string, bit: number): boolean {
  checkRange("feature bit", bit, 31);
  const info = parseKey(key, secret);
  if (info === null || info.features === undefined) return false;
  return featureBits(info.features).includes(bit);
}

// "product 42, 5 seats, until 2025-12-31" plus ", features 0,3" for version 2 keys; "invalid key".
export function describeKey(key: string, secret: string): string {
  const info = parseKey(key, secret);
  if (info === null) return "invalid key";
  const seats = `${info.seats} ${info.seats === 1 ? "seat" : "seats"}`;
  let text = `product ${info.product}, ${seats}, until ${isoDay(info.expiresDay)}`;
  if (info.features !== undefined) {
    const bits = featureBits(info.features);
    text += `, features ${bits.length === 0 ? "none" : bits.join(",")}`;
  }
  return text;
}
