import { crc32 } from "e7:encoding";

export interface Packet { offset: number; type: number; flags: number; seq: number; payload: Uint8Array }

const MAGIC = 0xe7;
const VERSION = 1;
const HEADER = 8;
const TRAILER = 4;

function isInt(x: number, max: number): boolean {
  return Number.isInteger(x) && x >= 0 && x <= max;
}

export function encodePacket(type: number, flags: number, seq: number, payload: Uint8Array): Uint8Array {
  if (!isInt(type, 255) || !isInt(flags, 255) || !isInt(seq, 65535)) throw new Error("invalid header field");
  if (payload.length > 65535) throw new Error("payload too large");
  const out = new Uint8Array(HEADER + payload.length + TRAILER);
  const view = new DataView(out.buffer);
  view.setUint8(0, MAGIC);
  view.setUint8(1, VERSION);
  view.setUint8(2, type);
  view.setUint8(3, flags);
  view.setUint16(4, seq);
  view.setUint16(6, payload.length);
  out.set(payload, HEADER);
  view.setUint32(HEADER + payload.length, crc32(out.subarray(0, HEADER + payload.length)));
  return out;
}

export function* readPackets(data: Uint8Array): Generator<Packet> {
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  let pos = 0;
  while (pos < data.length) {
    if (data.length - pos < HEADER) throw new Error(`truncated packet at ${pos}`);
    if (view.getUint8(pos) !== MAGIC) throw new Error(`bad magic at ${pos}`);
    const version = view.getUint8(pos + 1);
    if (version !== VERSION) throw new Error(`unsupported version ${version} at ${pos}`);
    const length = view.getUint16(pos + 6);
    const end = pos + HEADER + length;
    if (end + TRAILER > data.length) throw new Error(`truncated packet at ${pos}`);
    if (crc32(data.subarray(pos, end)) !== view.getUint32(end)) throw new Error(`crc mismatch at ${pos}`);
    yield {
      offset: pos,
      type: view.getUint8(pos + 2),
      flags: view.getUint8(pos + 3),
      seq: view.getUint16(pos + 4),
      payload: data.slice(pos + HEADER, end),
    };
    pos = end + TRAILER;
  }
}
