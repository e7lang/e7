import { assertEqual } from "e7:test";
import { decodePacket } from "./solution.ts";

export function testTemperatureHumidityBattery(): void {
  // version 2, flags 0, seq 300, temperature 21.5 °C, humidity 40.25 %, battery 87 %
  const p = new Uint8Array([2, 0, 0x2c, 0x01, 0x01, 2, 0x66, 0x08, 0x02, 2, 0xb9, 0x0f, 0x03, 1, 87]);
  assertEqual(decodePacket(p), '{"seq":300,"temperature":21.5,"humidity":40.25,"battery":87,"pressure":null,"alarm":false}');
}

export function testUnsupportedVersion(): void {
  assertEqual(decodePacket(new Uint8Array([1, 0, 0, 0])), '{"error":"unsupported version 1"}');
}
