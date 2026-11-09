import { assertEqual, assertThrows } from "e7:test";
import { hexDecode, hexEncode } from "e7:encoding";
import { decodePacket, encodePacket, summarize } from "./solution.ts";
import type { Packet } from "./solution.ts";

function sample(): Packet {
  return {
    deviceId: 0x01020304,
    readings: [
      { sensor: 1, time: 1700000010, value: 21.5 },
      { sensor: 2, time: 1700000000, value: -3.25 },
    ],
  };
}

export function testEncode(): void {
  assertEqual(hexEncode(encodePacket(sample())), "544c0102010203046553f100000a0141ac0000000002c0500000");
}

export function testRoundTrip(): void {
  assertEqual(decodePacket(encodePacket(sample())), sample());
}

export function testDecodeErrors(): void {
  assertThrows(() => { decodePacket(hexDecode("5858")); }, "bad magic");
  assertThrows(() => { decodePacket(hexDecode("544c0300")); }, "unsupported version 3");
  assertThrows(() => { decodePacket(encodePacket(sample()).subarray(0, 20)); }, "truncated packet");
}

export function testSummarize(): void {
  const p = sample();
  p.readings.push({ sensor: 1, time: 1700000020, value: 22 });
  assertEqual(summarize(p), "1=21.75 2=-3.25");
}
