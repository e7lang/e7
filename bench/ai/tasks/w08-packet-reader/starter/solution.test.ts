import { assertEqual, assertThrows } from "e7:test";
import { hexEncode, hexDecode, utf8Encode } from "e7:encoding";
import { encodePacket, readPackets } from "./solution.ts";

export function testEncode(): void {
  assertEqual(hexEncode(encodePacket(1, 0, 258, utf8Encode("hi"))), "e7010100010200026869c637065b");
}

export function testRead(): void {
  const packets = [...readPackets(hexDecode("e7010100010200026869c637065b" + "e7010280ffff0000d6f3db20"))];
  assertEqual(packets, [
    { offset: 0, type: 1, flags: 0, seq: 258, payload: utf8Encode("hi") },
    { offset: 14, type: 2, flags: 0x80, seq: 65535, payload: new Uint8Array(0) },
  ]);
}

export function testCorrupt(): void {
  assertThrows(() => { [...readPackets(hexDecode("e7010100010200026869c637065c"))]; }, "crc mismatch at 0");
}
