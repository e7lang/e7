Our BLE accessory streams framed packets to the phone, and we want the framing code in an E7 script shared by
the iOS and Android apps. Please write `solution.ts` with:

```ts
export interface Packet { offset: number; type: number; flags: number; seq: number; payload: Uint8Array }
export function encodePacket(type: number, flags: number, seq: number, payload: Uint8Array): Uint8Array
export function readPackets(data: Uint8Array): Generator<Packet>
```

A packet is (all multi-byte fields big-endian):

| offset | size | field |
|---|---|---|
| 0 | 1 | magic, always `0xE7` |
| 1 | 1 | version, always `1` |
| 2 | 1 | type |
| 3 | 1 | flags |
| 4 | 2 | seq (uint16) |
| 6 | 2 | payload length `n` (uint16) |
| 8 | n | payload |
| 8+n | 4 | CRC-32 (the zlib / IEEE one) of bytes `0 .. 8+n-1` (header and payload), uint32 |

`encodePacket` builds one packet. `type` and `flags` must be integers 0..255 and `seq` 0..65535, otherwise
throw `new Error("invalid header field")`; a payload longer than 65535 bytes throws
`new Error("payload too large")`.

`readPackets(data)` is a generator (`function*`) that yields the packets of a buffer holding zero or more
packets back to back. `offset` is where the packet starts in `data`; `payload` is a copy of the payload bytes.
It must be lazy: the stream can be long and the caller may stop early, so parse a packet only when the caller
asks for the next one, and a bad packet must only throw when iteration reaches it (the packets before it are
still delivered). Errors, checked in this order for the packet starting at offset `<o>`:

- fewer than 8 bytes left for the header → `truncated packet at <o>`
- first byte is not `0xE7` → `bad magic at <o>`
- version is not 1 → `unsupported version <v> at <o>`
- not enough bytes left for the payload and the CRC → `truncated packet at <o>`
- CRC does not match → `crc mismatch at <o>`

`data` may be a `subarray` view into a larger buffer; offsets are relative to `data`.
`solution.test.ts` has a few examples.
