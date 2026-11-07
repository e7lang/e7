// @error: module 'e7:crypto' has no exported member 'rsaSign'; e7:crypto has only modern algorithms (SHA-256/384/512, HMAC, HKDF, PBKDF2, AES-GCM, ChaCha20-Poly1305, Ed25519, X25519): register a host function for legacy ones
import * as crypto from "e7:crypto";
console.log(crypto.rsaSign("sha256", new Uint8Array(0), new Uint8Array(0)).length);
