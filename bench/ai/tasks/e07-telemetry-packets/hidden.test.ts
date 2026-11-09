import { assertEqual, assertThrows } from "e7:test";
import { crc32, hexDecode, hexEncode } from "e7:encoding";
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

function sampleV2(): Packet {
  return {
    deviceId: 0x01020304,
    readings: [
      { sensor: 1, time: 1700000010, value: 21.5, lowBattery: false },
      { sensor: 2, time: 1700000000, value: -3.25, lowBattery: true },
    ],
  };
}

const V1_HEX = "544c0102010203046553f100000a0141ac0000000002c0500000";
// header (count is a u16), then per reading: offset, sensor, flags, f32
const V2_BODY = "544c02" + "0002" + "01020304" + "6553f100" + "000a" + "01" + "00" + "41ac0000" + "0000" + "02" + "01" + "c0500000";

export function testVersion1Unchanged(): void {
  assertEqual(hexEncode(encodePacket(sample())), V1_HEX);
  assertEqual(hexEncode(encodePacket(sample(), 1)), V1_HEX);
  assertEqual(decodePacket(hexDecode(V1_HEX)), sample());
}

export function testVersion1IgnoresLowBattery(): void {
  assertEqual(hexEncode(encodePacket(sampleV2(), 1)), V1_HEX);
}

export function testEncodeVersion2Layout(): void {
  const bytes = encodePacket(sampleV2(), 2);
  assertEqual(bytes.length, 13 + 8 * 2 + 4);
  assertEqual(hexEncode(bytes.subarray(0, bytes.length - 4)), V2_BODY);
  const crc = crc32(hexDecode(V2_BODY));
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  assertEqual(view.getUint32(bytes.length - 4), crc);
}

export function testVersion2RoundTrip(): void {
  assertEqual(decodePacket(encodePacket(sampleV2(), 2)), sampleV2());
  // lowBattery missing in the input is encoded as false
  const decoded = decodePacket(encodePacket(sample(), 2));
  assertEqual(decoded.readings[0].lowBattery, false);
  assertEqual(decoded.readings[1].value, -3.25);
}

export function testVersion2ManyReadings(): void {
  const p: Packet = { deviceId: 7, readings: [] };
  for (let i = 0; i < 300; i++) p.readings.push({ sensor: i % 4, time: 1000 + i, value: i / 4, lowBattery: i === 299 });
  const bytes = encodePacket(p, 2);
  assertEqual(bytes.length, 13 + 8 * 300 + 4);
  assertEqual(hexEncode(bytes.subarray(3, 5)), "012c");
  assertEqual(decodePacket(bytes), p);
  assertThrows(() => { encodePacket(p); }, "too many readings");
}

export function testFloat32Precision(): void {
  const p: Packet = { deviceId: 1, readings: [{ sensor: 0, time: 5, value: 0.1, lowBattery: false }] };
  assertEqual(decodePacket(encodePacket(p, 2)).readings[0].value, 0.10000000149011612);
}

export function testChecksumMismatch(): void {
  const bytes = encodePacket(sampleV2(), 2);
  const bad = bytes.slice();
  bad[14] ^= 0x01; // a reading's time offset
  assertThrows(() => { decodePacket(bad); }, "checksum mismatch");
  const badCrc = bytes.slice();
  badCrc[badCrc.length - 1] ^= 0x80;
  assertThrows(() => { decodePacket(badCrc); }, "checksum mismatch");
}

export function testVersion2Truncated(): void {
  const bytes = encodePacket(sampleV2(), 2);
  assertThrows(() => { decodePacket(bytes.subarray(0, bytes.length - 1)); }, "truncated packet");
  assertThrows(() => { decodePacket(bytes.subarray(0, 12)); }, "truncated packet");
  const longer = new Uint8Array(bytes.length + 1);
  longer.set(bytes);
  assertThrows(() => { decodePacket(longer); }, "trailing bytes");
}

export function testDecodeErrorsRegression(): void {
  assertThrows(() => { decodePacket(hexDecode("54")); }, "bad magic");
  assertThrows(() => { decodePacket(hexDecode("544c")); }, "truncated packet");
  assertThrows(() => { decodePacket(hexDecode("544c03")); }, "unsupported version 3");
  assertThrows(() => { decodePacket(hexDecode("544c00")); }, "unsupported version 0");
  assertThrows(() => { decodePacket(hexDecode(V1_HEX + "00")); }, "trailing bytes");
}

export function testDecodeFromSubarray(): void {
  const framed = new Uint8Array(100);
  const pkt = encodePacket(sampleV2(), 2);
  framed.set(pkt, 7);
  assertEqual(decodePacket(framed.subarray(7, 7 + pkt.length)), sampleV2());
}

export function testFlagsOtherBitsIgnored(): void {
  const bytes = encodePacket(sampleV2(), 2);
  const body = bytes.slice(0, bytes.length - 4);
  body[13 + 3] = 0xfe; // first reading: other bits set, low battery bit clear
  body[13 + 8 + 3] = 0x03;
  const crc = crc32(body);
  const full = new Uint8Array(bytes.length);
  full.set(body);
  new DataView(full.buffer).setUint32(body.length, crc);
  const r = decodePacket(full).readings;
  assertEqual([r[0].lowBattery, r[1].lowBattery], [false, true]);
}

export function testEncodeErrors(): void {
  assertThrows(() => { encodePacket({ deviceId: -1, readings: [] }, 2); }, "bad device id");
  assertThrows(() => { encodePacket({ deviceId: 1, readings: [{ sensor: 256, time: 1, value: 0 }] }, 2); }, "bad sensor 256");
  assertThrows(() => { encodePacket({ deviceId: 1, readings: [{ sensor: 1, time: 1, value: 0 }, { sensor: 1, time: 70000, value: 0 }] }, 2); }, "time span too large");
}

export function testEmptyPacketVersion2(): void {
  const bytes = encodePacket({ deviceId: 9, readings: [] }, 2);
  assertEqual(hexEncode(bytes.subarray(0, 13)), "544c02" + "0000" + "00000009" + "00000000");
  assertEqual(decodePacket(bytes), { deviceId: 9, readings: [] });
}

export function testSummarizeLowBattery(): void {
  const p = sampleV2();
  p.readings.push({ sensor: 1, time: 1700000020, value: 22, lowBattery: true });
  p.readings.push({ sensor: 0, time: 1700000020, value: 1, lowBattery: true });
  assertEqual(summarize(p), "0=1.00 1=21.75 2=-3.25 low=0,1,2");
  assertEqual(summarize(sample()), "1=21.50 2=-3.25");
}
