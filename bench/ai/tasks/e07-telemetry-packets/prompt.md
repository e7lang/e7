`solution.ts` encodes and decodes the binary telemetry packets of our field sensors (format version 1 is
described at the top of the file). The new sensor firmware sends version 2 packets, and the app must handle
both. Please extend `solution.ts`:

```ts
export interface Reading {
  sensor: number;
  time: number;
  value: number;
  lowBattery?: boolean; // new
}
export type PacketVersion = 1 | 2;

export function encodePacket(p: Packet, version: PacketVersion = 1): Uint8Array   // new parameter
export function decodePacket(bytes: Uint8Array): Packet                         // accepts version 1 and 2
export function summarize(p: Packet): string
```

Version 2 layout (big-endian like version 1):

| Bytes | Field |
|---|---|
| 2 | magic `"TL"` (0x54 0x4c) |
| 1 | version: 2 |
| 2 | number of readings (u16, so up to 65535) |
| 4 | device id (u32) |
| 4 | base time (u32): the earliest reading's time, 0 when there are none |
| 8 per reading | u16 time offset from the base time, u8 sensor, u8 flags, f32 value |
| 4 | CRC-32 (`crc32` from `e7:encoding`) of all the bytes before it, as a u32 |

Flags: bit 0 (0x01) is "low battery"; the other bits are written as 0 and ignored when decoding.

- `encodePacket(p, 2)` writes `lowBattery === true` as the flag (missing means false). Version 1 output
  stays exactly as today and ignores `lowBattery`. The existing encode errors stay (`too many readings` now
  means more than 255 readings for version 1 and more than 65535 for version 2).
- `decodePacket` of a version 2 packet sets `lowBattery` (true or false) on every reading. Version 1
  readings come back without `lowBattery`, as today.
- Decode errors, checked in this order: `bad magic`, `truncated packet` (no version byte),
  `unsupported version <n>` (anything but 1 or 2), `truncated packet` (shorter than the header or than the
  readings plus the CRC need), `trailing bytes` (longer), `checksum mismatch`.
- `decodePacket` must work on a `subarray` of a larger buffer.
- `summarize`: as today, then, if any reading has `lowBattery === true`, append ` low=` and the numbers of
  those sensors, ascending, without duplicates, joined by `,` (`"0=1.00 1=21.75 2=-3.25 low=0,1,2"`).

`solution.test.ts` has the current tests.
