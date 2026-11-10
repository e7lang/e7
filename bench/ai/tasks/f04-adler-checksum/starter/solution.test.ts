import { assertEqual } from "e7:test";
import { adler32Hex, manifest } from "./solution.ts";

export function testKnownValue(): void {
  assertEqual(adler32Hex(new TextEncoder().encode("Wikipedia")), "11e60398");
}

export function testSmallManifest(): void {
  const image = new TextEncoder().encode("hello firmware!");
  assertEqual(manifest(image, 4), "0 4 041701a6\n4 4 0355015f\n8 4 046201b8\n12 3 024400f9\ntotal 2ee005b3");
}
