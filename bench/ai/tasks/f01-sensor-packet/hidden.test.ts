import { assertEqual } from "e7:test";
import { decodePacket } from "./solution.ts";

const HEADER = [2, 0, 0x2c, 0x01]; // version 2, seq 300

function packet(records: number[]): Uint8Array {
  return new Uint8Array([...HEADER, ...records]);
}

export function testAlarmAsLastRecord(): void {
  const p = packet([0x01, 2, 0x66, 0x08, 0x05, 0]);
  assertEqual(decodePacket(p), '{"seq":300,"temperature":21.5,"humidity":null,"battery":null,"pressure":null,"alarm":true}');
}

export function testAlarmOnly(): void {
  assertEqual(decodePacket(packet([0x05, 0])), '{"seq":300,"temperature":null,"humidity":null,"battery":null,"pressure":null,"alarm":true}');
}

export function testAlarmInTheMiddle(): void {
  assertEqual(decodePacket(packet([0x05, 0, 0x03, 1, 12])), '{"seq":300,"temperature":null,"humidity":null,"battery":12,"pressure":null,"alarm":true}');
}

export function testTrailingSingleByteIsTruncated(): void {
  assertEqual(decodePacket(packet([0x03, 1, 50, 0x01])), '{"error":"truncated record at 7"}');
}

export function testRecordHeaderWithoutValueIsTruncated(): void {
  assertEqual(decodePacket(packet([0x01, 2, 0x66, 0x08, 0x02, 2])), '{"error":"truncated record at 8"}');
}

export function testShortValueIsTruncated(): void {
  assertEqual(decodePacket(packet([0x04, 4, 0xcd, 0x8b])), '{"error":"truncated record at 4"}');
}

export function testAllFieldsNegativeTemperature(): void {
  const p = packet([0x04, 4, 0xcd, 0x8b, 0x01, 0x00, 0x01, 2, 0xf3, 0xfd, 0x02, 2, 0x10, 0x27, 0x03, 1, 100, 0x05, 0]);
  assertEqual(decodePacket(p), '{"seq":300,"temperature":-5.25,"humidity":100,"battery":100,"pressure":1013.25,"alarm":true}');
}

export function testUnknownTypesAreSkipped(): void {
  const p = packet([0x7f, 3, 0xaa, 0xbb, 0xcc, 0x03, 1, 64, 0x40, 0]);
  assertEqual(decodePacket(p), '{"seq":300,"temperature":null,"humidity":null,"battery":64,"pressure":null,"alarm":false}');
}

export function testBadLength(): void {
  assertEqual(decodePacket(packet([0x03, 1, 9, 0x01, 3, 0x66, 0x08, 0x00])), '{"error":"bad length for type 1 at 7"}');
  assertEqual(decodePacket(packet([0x05, 1, 1])), '{"error":"bad length for type 5 at 4"}');
}

export function testHeaderOnly(): void {
  assertEqual(decodePacket(new Uint8Array([2, 7, 0xff, 0xff])), '{"seq":65535,"temperature":null,"humidity":null,"battery":null,"pressure":null,"alarm":false}');
}

export function testTooShort(): void {
  assertEqual(decodePacket(new Uint8Array([2, 0, 1])), '{"error":"too short"}');
  assertEqual(decodePacket(new Uint8Array(0)), '{"error":"too short"}');
}

export function testPacketInsideLargerBuffer(): void {
  // the app passes a view into the whole advertisement
  const adv = new Uint8Array([0xff, 0xee, 0xdd, 2, 0, 0x05, 0x00, 0x03, 1, 33, 0x99, 0x99]);
  const p = adv.subarray(3, 10);
  assertEqual(decodePacket(p), '{"seq":5,"temperature":null,"humidity":null,"battery":33,"pressure":null,"alarm":false}');
}
