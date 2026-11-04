// @runtime-error: low-order public key
import { x25519 } from "e7:crypto";
console.log(x25519(new Uint8Array(32).fill(7), new Uint8Array(32)).length);
