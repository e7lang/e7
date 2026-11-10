import { assert, assertEqual, assertThrows } from "e7:test";
import { checkKey, dayNumber, describeKey, hasFeature, makeKey, parseKey } from "./solution.ts";

// Expected keys were computed with an independent implementation of the format.
const secret = "s3cret";
const V1 = "AAVA-RDYF-ABE2-56QN"; // product 42, 2025-12-31, 5 seats
const V2_F9 = "AIAC-UCEP-AACQ-AAAA-BGWI-45UM"; // product 42, 2025-12-31, 5 seats, features 0 and 3
const V2_F0 = "AIAC-UCEP-AACQ-AAAA-AC2Y-5BFX"; // same, no features
const V2_300_HIGH = "AIAC-UCEP-AEWI-AAAA-BH54-Y4N2"; // 300 seats, features 0, 3 and 31
const V2_300 = "AIAC-UCEP-AEWA-AAAA-AAFO-X3ZV"; // 300 seats, features 0 (value)
const V2_ALL = "AIAA-OAAA-AAA7-7777-77CH-4QX5"; // product 7, day 0, 1 seat, all 32 features
const V2_ONE = "AIAC-UCEP-AAAQ-AAAA-AGM7-CZAY"; // product 42, 2025-12-31, 1 seat, feature 0
const END = dayNumber("2025-12-31");

export function testVersion1Unchanged(): void {
  assertEqual(makeKey({ product: 42, expiresDay: END, seats: 5 }, secret), V1);
  assertEqual(parseKey(V1, secret), { product: 42, expiresDay: 2191, seats: 5 });
  assertEqual(checkKey(V1, secret, 42, END), "valid");
  assertEqual(checkKey("AAVA-RDYF-ABE2-56QM", secret, 42, 0), "bad checksum");
}

export function testMakeVersion2(): void {
  assertEqual(makeKey({ product: 42, expiresDay: END, seats: 5, features: 9 }, secret), V2_F9);
  assertEqual(makeKey({ product: 42, expiresDay: END, seats: 5, features: 0 }, secret), V2_F0);
  assertEqual(makeKey({ product: 7, expiresDay: 0, seats: 1, features: 0xffffffff }, secret), V2_ALL);
}

export function testManySeatsNeedVersion2(): void {
  assertEqual(makeKey({ product: 42, expiresDay: END, seats: 300 }, secret), V2_300);
  assertEqual(makeKey({ product: 42, expiresDay: END, seats: 255 }, secret).length, 19);
  assertEqual(parseKey(V2_300, secret), { product: 42, expiresDay: 2191, seats: 300, features: 0 });
}

export function testParseVersion2(): void {
  assertEqual(parseKey(V2_F9, secret), { product: 42, expiresDay: 2191, seats: 5, features: 9 });
  assertEqual(parseKey(" aiac ucep aacq aaaa bgwi 45um ", secret), { product: 42, expiresDay: 2191, seats: 5, features: 9 });
  assertEqual(parseKey(V2_F9, "other"), null);
}

export function testHighFeatureBitIsUnsigned(): void {
  assertEqual(parseKey(V2_300_HIGH, secret), { product: 42, expiresDay: 2191, seats: 300, features: 2147483657 });
  assertEqual(parseKey(V2_ALL, secret), { product: 7, expiresDay: 0, seats: 1, features: 4294967295 });
  assertEqual(makeKey({ product: 42, expiresDay: END, seats: 300, features: 2147483657 }, secret), V2_300_HIGH);
}

export function testCheckKeyVersion2(): void {
  assertEqual(checkKey(V2_F9, secret, 42, END), "valid");
  assertEqual(checkKey(V2_F9, secret, 42, END + 1), "expired");
  assertEqual(checkKey(V2_F9, secret, 41, END), "wrong product");
  assertEqual(checkKey(V2_F9, "other", 42, END), "bad checksum");
  assertEqual(checkKey("AIAC-UCEP-AACQ-AAAA-BGWI-45UN", secret, 42, END), "bad checksum");
}

export function testUnsupportedVersion(): void {
  // the same payload with version byte 3
  assertEqual(checkKey("AMAC-UCEP-AACQ-AAAA-BGWI-45UM", secret, 42, END), "unsupported version");
  assertEqual(parseKey("AMAC-UCEP-AACQ-AAAA-BGWI-45UM", secret), null);
  // version 1 lengths have no version byte
  assertEqual(checkKey("AMAC-UCEP-AACQ-AAAA", secret, 42, END), "bad checksum");
}

export function testMalformed(): void {
  assertEqual(checkKey("AIAC-UCEP-AACQ-AAAA-BGWI", secret, 42, END), "malformed");
  assertEqual(checkKey("AIAC-UCEP-AACQ-AAAA-BGWI-45U1", secret, 42, END), "malformed");
  assertEqual(checkKey("AIAC-UCEP-AACQ-AAAA-BGWI-45UM-AAAA", secret, 42, END), "malformed");
  assertEqual(parseKey("AIAC-UCEP-AACQ-AAAA-BGWI-45U8", secret), null);
  assertEqual(describeKey("not a key", secret), "invalid key");
}

export function testHasFeature(): void {
  assert(hasFeature(V2_F9, secret, 0));
  assert(hasFeature(V2_F9, secret, 3));
  assert(!hasFeature(V2_F9, secret, 1));
  assert(hasFeature(V2_300_HIGH, secret, 31));
  assert(!hasFeature(V2_300_HIGH, secret, 30));
  assert(hasFeature(V2_ALL, secret, 17));
  assert(!hasFeature(V2_F0, secret, 0));
  assert(!hasFeature(V1, secret, 0));
  assert(!hasFeature(V2_F9, "other", 0));
}

export function testBadFeatureBit(): void {
  assertThrows(() => { hasFeature(V2_F9, secret, 32); }, "bad feature bit");
  assertThrows(() => { hasFeature(V2_F9, secret, -1); }, "bad feature bit");
  assertThrows(() => { hasFeature(V2_F9, secret, 1.5); }, "bad feature bit");
}

export function testDescribeKey(): void {
  assertEqual(describeKey(V1, secret), "product 42, 5 seats, until 2025-12-31");
  assertEqual(describeKey(V2_F9, secret), "product 42, 5 seats, until 2025-12-31, features 0,3");
  assertEqual(describeKey(V2_F0, secret), "product 42, 5 seats, until 2025-12-31, features none");
  assertEqual(describeKey(V2_300_HIGH, secret), "product 42, 300 seats, until 2025-12-31, features 0,3,31");
  assertEqual(describeKey(V2_ONE, secret), "product 42, 1 seat, until 2025-12-31, features 0");
  const all: number[] = [];
  for (let i = 0; i < 32; i++) all.push(i);
  assertEqual(describeKey(V2_ALL, secret), `product 7, 1 seat, until 2020-01-01, features ${all.join(",")}`);
}

export function testMakeKeyErrors(): void {
  assertThrows(() => { makeKey({ product: 1, expiresDay: 1, seats: 1, features: -1 }, secret); }, "bad features");
  assertThrows(() => { makeKey({ product: 1, expiresDay: 1, seats: 1, features: 4294967296 }, secret); }, "bad features");
  assertThrows(() => { makeKey({ product: 1, expiresDay: 1, seats: 1, features: 0.5 }, secret); }, "bad features");
  assertThrows(() => { makeKey({ product: 1, expiresDay: 1, seats: 65536 }, secret); }, "bad seats");
  assertThrows(() => { makeKey({ product: 1, expiresDay: 1, seats: -1, features: 1 }, secret); }, "bad seats");
  assertThrows(() => { makeKey({ product: 65536, expiresDay: 1, seats: 1, features: 1 }, secret); }, "bad product");
}
