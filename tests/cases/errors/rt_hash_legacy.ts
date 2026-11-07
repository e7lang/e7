// @runtime-error: unsupported hash algorithm 'sha1' (use sha256, sha384 or sha512)
import { hmac } from "e7:crypto";
const alg: string = ["sha1"][0];
console.log(hmac(alg, new Uint8Array(1), new Uint8Array(1)).length);
