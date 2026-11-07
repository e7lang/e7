// @error: module 'e7:crypto' has no exported member 'crc32'; crc32 is in 'e7:encoding'
import { crc32 } from "e7:crypto";
console.log(crc32(new Uint8Array(0)));
