// @error: module 'e7:crypto' has no exported member 'md5'; e7:crypto has only modern algorithms (SHA-256/384/512, HMAC, HKDF, PBKDF2, AES-GCM, ChaCha20-Poly1305, Ed25519, X25519): register a host function for legacy ones
import { md5 } from "e7:crypto";
console.log(md5(new Uint8Array(0)).length);
