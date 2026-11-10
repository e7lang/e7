import { assert, assertEqual, assertThrows } from "e7:test";
import { concatBytes, hexEncode, hexDecode, utf8Encode } from "e7:encoding";
import { encodePacket, readPackets } from "./solution.ts";
import type { Packet } from "./solution.ts";

const A = "e7010100010200026869c637065b";       // type 1, seq 258, "hi"
const B = "e7010280ffff0000d6f3db20";           // type 2, flags 0x80, seq 65535, empty
const C = "e70107030001000300ff1037df9781";     // type 7, flags 3, seq 1, [0, 255, 16]

function all(hex: string): Packet[] {
  return [...readPackets(hexDecode(hex))];
}

function errorOf(hex: string): string {
  try {
    for (const p of readPackets(hexDecode(hex))) assert(p.offset >= 0);
  } catch (e) {
    if (e instanceof Error) return e.message;
  }
  return "no error";
}

export function testEncodeVectors(): void {
  assertEqual(hexEncode(encodePacket(2, 0x80, 65535, new Uint8Array(0))), B);
  assertEqual(hexEncode(encodePacket(7, 3, 1, new Uint8Array([0, 255, 16]))), C);
}

export function testEncodeLargePayload(): void {
  const payload = new Uint8Array(300).fill(0xab);
  const bytes = encodePacket(9, 1, 40000, payload);
  assertEqual(bytes.length, 312);
  assertEqual(hexEncode(bytes.subarray(0, 8)), "e7010901" + "9c40" + "012c");
  const p = [...readPackets(bytes)];
  assertEqual(p.length, 1);
  assertEqual(p[0].payload, payload);
  assertEqual(p[0].seq, 40000);
}

export function testEncodeLimits(): void {
  assertEqual(encodePacket(255, 255, 0, new Uint8Array(65535)).length, 65547);
  assertThrows(() => { encodePacket(1, 0, 0, new Uint8Array(65536)); }, "payload too large");
  assertThrows(() => { encodePacket(256, 0, 0, new Uint8Array(0)); }, "invalid header field");
  assertThrows(() => { encodePacket(1, -1, 0, new Uint8Array(0)); }, "invalid header field");
  assertThrows(() => { encodePacket(1, 0, 65536, new Uint8Array(0)); }, "invalid header field");
  assertThrows(() => { encodePacket(1.5, 0, 0, new Uint8Array(0)); }, "invalid header field");
}

export function testReadThreePackets(): void {
  const ps = all(A + B + C);
  assertEqual(ps.map((p) => p.offset), [0, 14, 26]);
  assertEqual(ps.map((p) => p.type), [1, 2, 7]);
  assertEqual(ps[2], { offset: 26, type: 7, flags: 3, seq: 1, payload: new Uint8Array([0, 255, 16]) });
}

export function testEmptyInput(): void {
  assertEqual(all(""), []);
}

export function testRoundTrip(): void {
  const parts: Uint8Array[] = [];
  for (let i = 0; i < 20; i++) parts.push(encodePacket(i, i * 3, i * 1000, utf8Encode("x".repeat(i))));
  const ps = [...readPackets(concatBytes(...parts))];
  assertEqual(ps.length, 20);
  for (let i = 0; i < 20; i++) {
    assertEqual([ps[i].type, ps[i].flags, ps[i].seq, ps[i].payload.length], [i, i * 3, i * 1000, i]);
  }
}

export function testPayloadIsCopy(): void {
  const data = hexDecode(A);
  const p = [...readPackets(data)][0];
  data[8] = 0;
  assertEqual(p.payload, utf8Encode("hi"));
}

export function testSubarrayInput(): void {
  const big = hexDecode("ffff" + A + C + "ffff");
  const ps = [...readPackets(big.subarray(2, big.length - 2))];
  assertEqual(ps.map((p) => p.offset), [0, 14]);
  assertEqual(ps[1].payload, new Uint8Array([0, 255, 16]));
}

export function testErrors(): void {
  assertEqual(errorOf("e70101"), "truncated packet at 0");
  assertEqual(errorOf(A + "e7010100"), "truncated packet at 14");
  assertEqual(errorOf("e6010100010200026869c637065b"), "bad magic at 0");
  assertEqual(errorOf(A + "e7020100010200026869c637065b"), "unsupported version 2 at 14");
  assertEqual(errorOf(A + B + "e70107030001000300ff1037df97"), "truncated packet at 26");
  assertEqual(errorOf(A + "e7010100010200026869c637065b00"), "truncated packet at 28");
  assertEqual(errorOf(B + "e7010100010200026869c737065b"), "crc mismatch at 12");
  assertEqual(errorOf(B + "e7010100010200026869c637065a"), "crc mismatch at 12");
}

export function testCheckOrder(): void {
  // short header is reported before a bad magic byte
  assertEqual(errorOf("00000000"), "truncated packet at 0");
  // bad magic before version
  assertEqual(errorOf("00020000000000000000000000"), "bad magic at 0");
  // version before the length check
  assertEqual(errorOf("e7090000000000ff"), "unsupported version 9 at 0");
}

export function testLazy(): void {
  // the second packet is corrupt, but the caller only wants the first
  const it = readPackets(hexDecode(A + "00"));
  const first = it.next();
  assertEqual(first.done, false);
  assertEqual(first.value?.seq, 258);
  assertThrows(() => { it.next(); }, "truncated packet at 14");
  // early exit from for...of never touches the bad packet
  let seen = 0;
  for (const p of readPackets(hexDecode(A + B + "deadbeef"))) {
    seen++;
    if (p.offset === 14) break;
  }
  assertEqual(seen, 2);
}
