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

export interface Reading {
  sensor: number;
  time: number;
  value: number;
}

export interface Packet {
  deviceId: number;
  readings: Reading[];
}

const MAGIC_0 = 0x54;
const MAGIC_1 = 0x4c;
const HEADER_SIZE = 12;
const READING_SIZE = 7;

function isUint(x: number, max: number): boolean {
  return Number.isInteger(x) && x >= 0 && x <= max;
}

export function encodePacket(p: Packet): Uint8Array {
  if (!isUint(p.deviceId, 0xffffffff)) throw new Error("bad device id");
  if (p.readings.length > 255) throw new Error("too many readings");
  let base = 0;
  if (p.readings.length > 0) base = Math.min(...p.readings.map((r) => r.time));
  const bytes = new Uint8Array(HEADER_SIZE + READING_SIZE * p.readings.length);
  const view = new DataView(bytes.buffer);
  view.setUint8(0, MAGIC_0);
  view.setUint8(1, MAGIC_1);
  view.setUint8(2, 1);
  view.setUint8(3, p.readings.length);
  view.setUint32(4, p.deviceId);
  view.setUint32(8, base);
  let pos = HEADER_SIZE;
  for (const r of p.readings) {
    if (!isUint(r.time, 0xffffffff)) throw new Error(`bad time ${r.time}`);
    if (r.time - base > 0xffff) throw new Error("time span too large");
    if (!isUint(r.sensor, 255)) throw new Error(`bad sensor ${r.sensor}`);
    view.setUint16(pos, r.time - base);
    view.setUint8(pos + 2, r.sensor);
    view.setFloat32(pos + 3, r.value);
    pos += READING_SIZE;
  }
  return bytes;
}

export function decodePacket(bytes: Uint8Array): Packet {
  if (bytes.length < 2 || bytes[0] !== MAGIC_0 || bytes[1] !== MAGIC_1) throw new Error("bad magic");
  if (bytes.length < 3) throw new Error("truncated packet");
  if (bytes[2] !== 1) throw new Error(`unsupported version ${bytes[2]}`);
  if (bytes.length < HEADER_SIZE) throw new Error("truncated packet");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const count = view.getUint8(3);
  const expected = HEADER_SIZE + READING_SIZE * count;
  if (bytes.length < expected) throw new Error("truncated packet");
  if (bytes.length > expected) throw new Error("trailing bytes");
  const deviceId = view.getUint32(4);
  const base = view.getUint32(8);
  const readings: Reading[] = [];
  for (let i = 0; i < count; i++) {
    const pos = HEADER_SIZE + READING_SIZE * i;
    readings.push({ sensor: view.getUint8(pos + 2), time: base + view.getUint16(pos), value: view.getFloat32(pos + 3) });
  }
  return { deviceId, readings };
}

// Average value per sensor, "sensor=avg" sorted by sensor number, averages with 2 decimals.
export function summarize(p: Packet): string {
  const sums = new Map<number, number[]>();
  for (const r of p.readings) {
    const s = sums.get(r.sensor);
    if (s === undefined) sums.set(r.sensor, [r.value, 1]);
    else {
      s[0] += r.value;
      s[1] += 1;
    }
  }
  const sensors = [...sums.keys()].sort((a, b) => a - b);
  return sensors.map((id) => {
    const s = sums.get(id) ?? [0, 1];
    return `${id}=${(s[0] / s[1]).toFixed(2)}`;
  }).join(" ");
}
