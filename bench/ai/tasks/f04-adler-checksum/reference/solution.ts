// Checksums for the firmware update manifest. The bootloader checks every block of the image with Adler-32
// (RFC 1950, as computed by zlib's adler32()), so the values here must match zlib bit for bit.

const MOD: u32 = 65521; // largest prime below 2^16

// Bytes summed between two reductions modulo MOD. 5552 is the largest n for which
// 255 * n * (n + 1) / 2 + (n + 1) * (MOD - 1) stays below 2^32, so `a` and `b` cannot wrap within a chunk.
const CHUNK = 5552;

export class Adler32 {
  private a: u32 = 1;
  private b: u32 = 0;

  update(data: Uint8Array): Adler32 {
    let a = this.a;
    let b = this.b;
    let i = 0;
    while (i < data.length) {
      const end = Math.min(i + CHUNK, data.length);
      for (; i < end; i++) {
        a += u32(data[i]);
        b += a;
      }
      a %= MOD;
      b %= MOD;
    }
    this.a = a;
    this.b = b;
    return this;
  }

  digest(): u32 {
    return (this.b << 16) | this.a;
  }

  hex(): string {
    return this.digest().toString(16).padStart(8, "0");
  }
}

export function adler32Hex(data: Uint8Array): string {
  return new Adler32().update(data).hex();
}

// True when `block` has the checksum `expectedHex` from a manifest (hex digits in either case).
export function verifyBlock(block: Uint8Array, expectedHex: string): boolean {
  return adler32Hex(block) === expectedHex.trim().toLowerCase();
}

// One line per block: "<offset> <length> <adler32>", then "total <adler32 of the whole image>".
export function manifest(image: Uint8Array, blockSize: number): string {
  if (!Number.isInteger(blockSize) || blockSize <= 0) throw new Error(`invalid block size ${blockSize}`);
  const lines: string[] = [];
  const total = new Adler32();
  for (let offset = 0; offset < image.length; offset += blockSize) {
    const block = image.subarray(offset, Math.min(offset + blockSize, image.length));
    lines.push(`${offset} ${block.length} ${adler32Hex(block)}`);
    total.update(block);
  }
  lines.push(`total ${total.hex()}`);
  return lines.join("\n");
}
