// Decoder for the advertisement packets of our BLE environment sensors (firmware 2.x).
//
// Layout (all multi-byte values little-endian):
//   0     version (u8, must be 2)
//   1     flags (u8, reserved)
//   2..3  sequence number (u16)
//   4..   records: type (u8), length (u8), then `length` value bytes

const VERSION = 2;
const HEADER_LEN = 4;
const RECORD_HEADER_LEN = 2;

const T_TEMPERATURE = 0x01; // i16, 1/100 °C
const T_HUMIDITY = 0x02; // u16, 1/100 %
const T_BATTERY = 0x03; // u8, percent
const T_PRESSURE = 0x04; // u32, Pa
const T_ALARM = 0x05; // no value: the alarm is active

interface Reading {
  seq: number;
  temperature: number | null;
  humidity: number | null;
  battery: number | null;
  pressure: number | null; // hPa
  alarm: boolean;
}

interface DecodeError {
  error: string;
}

function fail(message: string): string {
  const e: DecodeError = { error: message };
  return JSON.stringify(e);
}

function expectedLength(type: number): number {
  switch (type) {
    case T_TEMPERATURE:
    case T_HUMIDITY:
      return 2;
    case T_BATTERY:
      return 1;
    case T_PRESSURE:
      return 4;
    case T_ALARM:
      return 0;
  }
  return -1; // unknown type: any length, skipped
}

export function decodePacket(packet: Uint8Array): string {
  if (packet.length < HEADER_LEN) return fail("too short");
  if (packet[0] !== VERSION) return fail(`unsupported version ${packet[0]}`);

  const view = new DataView(packet.buffer, packet.byteOffset, packet.byteLength);
  const reading: Reading = {
    seq: view.getUint16(2, true),
    temperature: null,
    humidity: null,
    battery: null,
    pressure: null,
    alarm: false,
  };

  let pos = HEADER_LEN;
  while (pos + RECORD_HEADER_LEN < packet.length) {
    const type = packet[pos];
    const len = packet[pos + 1];
    const start = pos + RECORD_HEADER_LEN;
    if (start + len > packet.length) return fail(`truncated record at ${pos}`);

    const want = expectedLength(type);
    if (want >= 0 && len !== want) return fail(`bad length for type ${type} at ${pos}`);

    switch (type) {
      case T_TEMPERATURE:
        reading.temperature = view.getInt16(start, true) / 100;
        break;
      case T_HUMIDITY:
        reading.humidity = view.getUint16(start, true) / 100;
        break;
      case T_BATTERY:
        reading.battery = view.getUint8(start);
        break;
      case T_PRESSURE:
        reading.pressure = view.getUint32(start, true) / 100;
        break;
      case T_ALARM:
        reading.alarm = true;
        break;
    }
    pos = start + len;
  }
  return JSON.stringify(reading);
}
