// @error: module 'e7:crypto' has no exported member 'hamc'; did you mean 'hmac'?
import * as crypto from "e7:crypto";
console.log(crypto.hamc("sha256", new Uint8Array(1), new Uint8Array(1)).length);
