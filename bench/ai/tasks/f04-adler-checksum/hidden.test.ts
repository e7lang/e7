import { assertEqual, assertThrows } from "e7:test";
import { Adler32, adler32Hex, manifest, verifyBlock } from "./solution.ts";

// Expected values are zlib's adler32().

function filled(n: number, value: number): Uint8Array {
  return new Uint8Array(n).fill(value);
}

function pattern(n: number): Uint8Array {
  const d = new Uint8Array(n);
  for (let i = 0; i < n; i++) d[i] = (i * 31 + 7) % 256;
  return d;
}

function text(n: number): Uint8Array {
  const d = new Uint8Array(n);
  for (let i = 0; i < n; i++) d[i] = 97 + ((i * i) % 26);
  return d;
}

export function testErasedFlashBlock(): void {
  assertEqual(adler32Hex(filled(16384, 0xff)), "b0d9c3b2");
}

export function testErased8K(): void {
  assertEqual(adler32Hex(filled(8192, 0xff)), "f4a3e1d2");
  assertEqual(adler32Hex(filled(6000, 0xff)), "a49759ea");
}

export function testErased5552(): void {
  assertEqual(adler32Hex(filled(5552, 0xff)), "f18f9b8c");
}

export function testLargeBinary(): void {
  assertEqual(adler32Hex(pattern(65536)), "7b2e8772");
}

export function testLargeText(): void {
  assertEqual(adler32Hex(text(20000)), "17356c8c");
}

export function testShortVectors(): void {
  assertEqual(adler32Hex(new TextEncoder().encode("abc")), "024d0127");
  assertEqual(adler32Hex(new Uint8Array(0)), "00000001");
}

export function testStreamingInSmallPieces(): void {
  const data = text(20000);
  const h = new Adler32();
  for (let i = 0; i < data.length; i += 1000) h.update(data.subarray(i, i + 1000));
  assertEqual(h.hex(), "17356c8c");
}

export function testManifest16K(): void {
  const image = new Uint8Array(40000);
  for (let i = 0; i < image.length; i++) image[i] = i < 30000 ? (i * 131 + 17) % 256 : 255;
  assertEqual(manifest(image, 16384), "0 16384 c3d3e1d2\n16384 16384 403c4344\n32768 7232 72622565\ntotal 5fbf4a88");
}

export function testManifestSmallBlocks(): void {
  const image = new TextEncoder().encode("hello firmware!");
  assertEqual(manifest(image, 4), "0 4 041701a6\n4 4 0355015f\n8 4 046201b8\n12 3 024400f9\ntotal 2ee005b3");
  assertEqual(manifest(new Uint8Array(0), 4), "total 00000001");
}

export function testInvalidBlockSize(): void {
  assertThrows(() => { manifest(filled(8, 1), 0); }, "invalid block size 0");
}

export function testVerifyBlock(): void {
  assertEqual(verifyBlock(filled(16384, 0xff), "B0D9C3B2"), true);
  assertEqual(verifyBlock(new TextEncoder().encode("abc"), " 024d0127 "), true);
  assertEqual(verifyBlock(new TextEncoder().encode("abd"), "024d0127"), false);
}
