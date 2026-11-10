// Binary telemetry packets sent by the field sensors over BLE. The app decodes them and also encodes
// packets for the gateway simulator. All multi-byte numbers are big-endian.
//
// Version 1 layout:
//   "T" "L"        magic (0x54 0x4c)
//   u8             version (1)
//   u8             number of readings
//   u32            device id
//   u32            base time (Unix seconds) = the earliest reading's time (0 when there are none)
//   per reading:   u16 time offset from the base time, u8 sensor, f32 value
//
// Version 2 layout:
//   "T" "L"        magic
//   u8             version (2)
//   u16            number of readings
//   u32            device id
//   u32            base time
//   per reading:   u16 time offset, u8 sensor, u8 flags (bit 0: low battery), f32 value
//   u32            CRC-32 of all the bytes before it
import { crc32 } from "e7:encoding";

export interface Reading {
  sensor: number;
  time: number;
  value: number;
  lowBattery?: boolean; // version 2 only
}

export type PacketVersion = 1 | 2;

export interface Packet {
  deviceId: number;
  readings: Reading[];
}

const MAGIC_0 = 0x54;
const MAGIC_1 = 0x4c;
const FLAG_LOW_BATTERY = 0x01;

interface Layout {
  header: number;
  reading: number;
  trailer: number;
  maxReadings: number;
}

function layoutOf(version: PacketVersion): Layout {
  switch (version) {
    case 1:
      return { header: 12, reading: 7, trailer: 0, maxReadings: 255 };
    case 2:
      return { header: 13, reading: 8, trailer: 4, maxReadings: 65535 };
  }
}

function isUint(x: number, max: number): boolean {
  return Number.isInteger(x) && x >= 0 && x <= max;
}

export function encodePacket(p: Packet, version: PacketVersion = 1): Uint8Array {
  const layout = layoutOf(version);
  if (!isUint(p.deviceId, 0xffffffff)) throw new Error("bad device id");
  if (p.readings.length > layout.maxReadings) throw new Error("too many readings");
  let base = 0;
  if (p.readings.length > 0) base = Math.min(...p.readings.map((r) => r.time));
  const bytes = new Uint8Array(layout.header + layout.reading * p.readings.length + layout.trailer);
  const view = new DataView(bytes.buffer);
  view.setUint8(0, MAGIC_0);
  view.setUint8(1, MAGIC_1);
  view.setUint8(2, version);
  let pos = 3;
  if (version === 1) view.setUint8(pos++, p.readings.length);
  else {
    view.setUint16(pos, p.readings.length);
    pos += 2;
  }
  view.setUint32(pos, p.deviceId);
  view.setUint32(pos + 4, base);
  pos += 8;
  for (const r of p.readings) {
    if (!isUint(r.time, 0xffffffff)) throw new Error(`bad time ${r.time}`);
    if (r.time - base > 0xffff) throw new Error("time span too large");
    if (!isUint(r.sensor, 255)) throw new Error(`bad sensor ${r.sensor}`);
    view.setUint16(pos, r.time - base);
    view.setUint8(pos + 2, r.sensor);
    if (version === 2) view.setUint8(pos + 3, r.lowBattery === true ? FLAG_LOW_BATTERY : 0);
    view.setFloat32(pos + layout.reading - 4, r.value);
    pos += layout.reading;
  }
  if (layout.trailer > 0) view.setUint32(pos, crc32(bytes.subarray(0, pos)));
  return bytes;
}

export function decodePacket(bytes: Uint8Array): Packet {
  if (bytes.length < 2 || bytes[0] !== MAGIC_0 || bytes[1] !== MAGIC_1) throw new Error("bad magic");
  if (bytes.length < 3) throw new Error("truncated packet");
  let version: PacketVersion;
  if (bytes[2] === 1) version = 1;
  else if (bytes[2] === 2) version = 2;
  else throw new Error(`unsupported version ${bytes[2]}`);
  const layout = layoutOf(version);
  if (bytes.length < layout.header) throw new Error("truncated packet");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const count = version === 1 ? view.getUint8(3) : view.getUint16(3);
  const expected = layout.header + layout.reading * count + layout.trailer;
  if (bytes.length < expected) throw new Error("truncated packet");
  if (bytes.length > expected) throw new Error("trailing bytes");
  if (layout.trailer > 0 && view.getUint32(expected - 4) !== crc32(bytes.subarray(0, expected - 4))) {
    throw new Error("checksum mismatch");
  }
  const deviceId = view.getUint32(layout.header - 8);
  const base = view.getUint32(layout.header - 4);
  const readings: Reading[] = [];
  for (let i = 0; i < count; i++) {
    const pos = layout.header + layout.reading * i;
    const r: Reading = { sensor: view.getUint8(pos + 2), time: base + view.getUint16(pos), value: view.getFloat32(pos + layout.reading - 4) };
    if (version === 2) r.lowBattery = (view.getUint8(pos + 3) & FLAG_LOW_BATTERY) !== 0;
    readings.push(r);
  }
  return { deviceId, readings };
}

// Average value per sensor, "sensor=avg" sorted by sensor number, averages with 2 decimals,
// then " low=<sensors>" when readings report a low battery.
export function summarize(p: Packet): string {
  const sums = new Map<number, number[]>();
  const low = new Set<number>();
  for (const r of p.readings) {
    if (r.lowBattery === true) low.add(r.sensor);
    const s = sums.get(r.sensor);
    if (s === undefined) sums.set(r.sensor, [r.value, 1]);
    else {
      s[0] += r.value;
      s[1] += 1;
    }
  }
  const sensors = [...sums.keys()].sort((a, b) => a - b);
  const text = sensors.map((id) => {
    const s = sums.get(id) ?? [0, 1];
    return `${id}=${(s[0] / s[1]).toFixed(2)}`;
  }).join(" ");
  if (low.size === 0) return text;
  return `${text} low=${[...low].sort((a, b) => a - b).join(",")}`;
}
