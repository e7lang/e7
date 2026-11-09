**Bug: devices reject firmware images because of a checksum mismatch**

Since we switched the update manifest to 16 KiB blocks, the bootloader rejects most firmware images: it
recomputes the Adler-32 of each block (zlib's `adler32()`) and gets a different value from the one in the
manifest. `solution.ts` (E7) builds the manifest. Small test images with small blocks still work.

Reproduce with a block of erased flash (16384 bytes of `0xff`):

```ts
adler32Hex(new Uint8Array(16384).fill(0xff))
```

Expected (zlib, and the bootloader): `"b0d9c3b2"`. Actual: a different value.

Please fix `solution.ts` so that every checksum matches zlib's Adler-32 for inputs of any length. The exported
API and the manifest format stay as they are:

```ts
export class Adler32 {
  update(data: Uint8Array): Adler32   // can be called several times; the result covers all data so far
  digest(): u32
  hex(): string                       // digest as 8 lowercase hex digits
}
export function adler32Hex(data: Uint8Array): string
export function manifest(image: Uint8Array, blockSize: number): string
export function verifyBlock(block: Uint8Array, expectedHex: string): boolean
```

The manifest has one line `"<offset> <length> <adler32>"` per block and a last line `"total <adler32>"` for the
whole image; a block size that is not a positive integer throws `invalid block size <n>`. `solution.test.ts`
has the existing tests.
