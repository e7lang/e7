**Bug: alarm from the environment sensors is sometimes not shown**

`solution.ts` (E7) decodes the advertisement packets of our BLE environment sensors; the packet layout is
described at the top of the file. Since firmware 2.3 the sensors put the alarm record last, and the app no longer
shows alarms from them.

Steps to reproduce: decode this packet (version 2, sequence 300, temperature 21.5 °C, then the alarm record,
type `0x05` with length 0):

```ts
decodePacket(new Uint8Array([2, 0, 0x2c, 0x01, 0x01, 2, 0x66, 0x08, 0x05, 0]))
```

Expected:

```
{"seq":300,"temperature":21.5,"humidity":null,"battery":null,"pressure":null,"alarm":true}
```

Actual: the same, but with `"alarm":false`. When the alarm record comes before the other records it works.

While testing this, QA also found that a packet that is cut off inside a record's two header bytes (for
example a single stray byte after the last record) decodes without any error. Every cut-off record must give
`{"error":"truncated record at <offset>"}`, where `<offset>` is the index of the record's type byte, as it
already does when a record's value is cut off.

Please fix `solution.ts`. The exported API stays as it is:

```ts
export function decodePacket(packet: Uint8Array): string
```

Everything that already works (the other record types, unknown record types being skipped, the other error
messages, packets passed as a view into a larger buffer) must keep working. `solution.test.ts` has the existing
tests.
