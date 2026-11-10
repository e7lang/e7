// @error: module 'e7:crypto' has no exported member 'hmacSha256'; it exports KeyHandle, HashName, sha256
// a missing export of a builtin module without a close name lists what the module exports
import { hmacSha256 } from "e7:crypto";
console.log(hmacSha256);
