// Type declarations for E7-specific features so that E7 programs type-check with the standard
// TypeScript compiler / IDEs. Add this file to your tsconfig "files" (or a triple-slash reference).
//
// At run time on a JavaScript engine the integer types behave like `number` (no wrapping);
// on e7vm they are true machine integers. Code that wraps results explicitly, e.g.
// `u32(a + b)`, behaves identically on both.

/** Fixed-width integers (E7 machine integers; aliases of number for the TypeScript checker). */
declare type i8 = number;
declare type u8 = number;
declare type i16 = number;
declare type u16 = number;
declare type i32 = number;
declare type u32 = number;
declare type i64 = number;
declare type u64 = number;
declare type int = number;

/** Conversions (wrap modulo 2^n, truncate fractions). */
declare function i8(x: number | bigint | boolean): i8;
declare function u8(x: number | bigint | boolean): u8;
declare function i16(x: number | bigint | boolean): i16;
declare function u16(x: number | bigint | boolean): u16;
declare function i32(x: number | bigint | boolean): i32;
declare function u32(x: number | bigint | boolean): u32;
declare function i64(x: number | bigint | boolean): i64;
declare function u64(x: number | bigint | boolean): u64;
declare function int(x: number | bigint | boolean): int;

/** Bit operations (single VM instructions). */
declare function rotl32(x: u32 | i32, n: number): u32;
declare function rotr32(x: u32 | i32, n: number): u32;
declare function rotl64(x: u64 | i64, n: number): u64;
declare function rotr64(x: u64 | i64, n: number): u64;
declare function clz32(x: number): number;
declare function clz64(x: number): number;
declare function ctz32(x: number): number;
declare function ctz64(x: number): number;
declare function popcnt(x: number): number;
declare function bswap16(x: number): number;
declare function bswap32(x: number): number;
declare function bswap64(x: number): number;
/** High 64 bits of the 128-bit product (unsigned / signed). */
declare function mulhi64(a: u64, b: u64): u64;
declare function imulhi64(a: i64, b: i64): i64;
/** Truncating division / remainder on numbers. */
declare function idiv(a: number, b: number): number;
declare function imod(a: number, b: number): number;
/** Print values (same as console.log). */
declare function print(...values: unknown[]): void;
/** Terminate the script with an error message (cannot be caught). */
declare function abort(message?: string): never;

declare module "e7:encoding" {
  export function hexEncode(data: Uint8Array): string;
  export function hexDecode(hex: string): Uint8Array;
  export function base64Encode(data: Uint8Array): string;
  export function base64Decode(b64: string): Uint8Array;
  export function base64UrlEncode(data: Uint8Array): string;
  export function base64UrlDecode(b64url: string): Uint8Array;
  export function base32Encode(data: Uint8Array): string;
  export function base32Decode(b32: string): Uint8Array;
  export function utf8Encode(text: string): Uint8Array;
  export function utf8Decode(data: Uint8Array): string;
  /** Decodes the first PEM block (optionally only blocks with the given label). */
  export function pemDecode(pem: string, label?: string | null): Uint8Array;
  export function pemEncode(der: Uint8Array, label: string): string;
  export function concatBytes(...parts: Uint8Array[]): Uint8Array;
  export function bytesEqual(a: Uint8Array, b: Uint8Array): boolean;
}

declare module "e7:crypto" {
  /** Opaque reference to a key held by the host (Keychain / Keystore / DPAPI). */
  export interface KeyHandle { readonly __keyHandle: unique symbol }
  /** "md5" | "sha1" | "sha224" | "sha256" | "sha384" | "sha512" | "sha3-224" | "sha3-256" | "sha3-384" | "sha3-512" | "keccak256" */
  export type HashName = string;
  export function md5(data: Uint8Array): Uint8Array;
  export function sha1(data: Uint8Array): Uint8Array;
  export function sha224(data: Uint8Array): Uint8Array;
  export function sha256(data: Uint8Array): Uint8Array;
  export function sha384(data: Uint8Array): Uint8Array;
  export function sha512(data: Uint8Array): Uint8Array;
  export function sha3_224(data: Uint8Array): Uint8Array;
  export function sha3_256(data: Uint8Array): Uint8Array;
  export function sha3_384(data: Uint8Array): Uint8Array;
  export function sha3_512(data: Uint8Array): Uint8Array;
  export function keccak256(data: Uint8Array): Uint8Array;
  export function hash(alg: HashName, data: Uint8Array): Uint8Array;
  export function hmac(alg: HashName, key: Uint8Array | KeyHandle, data: Uint8Array): Uint8Array;
  export function hkdf(alg: HashName, ikm: Uint8Array, salt: Uint8Array, info: Uint8Array, length: number): Uint8Array;
  export function pbkdf2(alg: HashName, password: Uint8Array, salt: Uint8Array, iterations: number, length: number): Uint8Array;
  export function crc32(data: Uint8Array): number;
  export function randomBytes(n: number): Uint8Array;
  export function timingSafeEqual(a: Uint8Array, b: Uint8Array): boolean;
  /** Returns ciphertext || 16-byte tag. */
  export function aesGcmEncrypt(key: Uint8Array, iv: Uint8Array, plaintext: Uint8Array, aad?: Uint8Array | null): Uint8Array;
  /** Returns null if authentication fails. */
  export function aesGcmDecrypt(key: Uint8Array, iv: Uint8Array, data: Uint8Array, aad?: Uint8Array | null): Uint8Array | null;
  export function chacha20Poly1305Encrypt(key: Uint8Array, nonce: Uint8Array, plaintext: Uint8Array, aad?: Uint8Array | null): Uint8Array;
  export function chacha20Poly1305Decrypt(key: Uint8Array, nonce: Uint8Array, data: Uint8Array, aad?: Uint8Array | null): Uint8Array | null;
  /** AES-CBC with PKCS#7 padding. */
  export function aesCbcEncrypt(key: Uint8Array, iv: Uint8Array, plaintext: Uint8Array): Uint8Array;
  export function aesCbcDecrypt(key: Uint8Array, iv: Uint8Array, data: Uint8Array): Uint8Array | null;
  /** AES-CTR with a 128-bit big-endian counter block. */
  export function aesCtr(key: Uint8Array, counter: Uint8Array, data: Uint8Array): Uint8Array;
  export function ed25519PublicKey(seed: Uint8Array): Uint8Array;
  export function ed25519Sign(seed: Uint8Array, message: Uint8Array): Uint8Array;
  export function ed25519Verify(publicKey: Uint8Array, message: Uint8Array, signature: Uint8Array): boolean;
  export function x25519(privateKey: Uint8Array, publicKey: Uint8Array): Uint8Array;
  /** curve: "P-256" | "P-384" | "P-521"; private key: raw scalar, SEC1 or PKCS#8 DER; signature: raw r||s. */
  export function ecdsaSign(curve: string, hash: HashName, privateKey: Uint8Array, message: Uint8Array): Uint8Array;
  /** public key: uncompressed point or SPKI DER; signature: raw r||s or DER. */
  export function ecdsaVerify(curve: string, hash: HashName, publicKey: Uint8Array, message: Uint8Array, signature: Uint8Array): boolean;
  export function ecdsaPublicKey(curve: string, privateKey: Uint8Array): Uint8Array;
  /** public key: SPKI or PKCS#1 DER; padding: "pkcs1" (default) or "pss". */
  export function rsaVerify(hash: HashName, publicKey: Uint8Array, message: Uint8Array, signature: Uint8Array, padding?: string | null): boolean;
  /** private key: PKCS#1 or PKCS#8 DER. */
  export function rsaSign(hash: HashName, privateKey: Uint8Array, message: Uint8Array, padding?: string | null): Uint8Array;
}

/** Assertion helpers for `e7 test` (usable in any E7 program). A failed assertion is a runtime
 *  error: it stops the test (or script) with a message and a stack trace. */
declare module "e7:test" {
  /** Fails unless `condition` is true. */
  export function assert(condition: boolean, message?: string | null): void;
  /** Fails unless `actual` and `expected` are structurally equal: primitives by value (NaN equals
   *  NaN), strings / bigints / Uint8Arrays by content, arrays element-wise, objects and class
   *  instances field by field, Maps and Sets by entries. Both arguments must have the same type. */
  export function assertEqual<T>(actual: T, expected: T, message?: string | null): void;
  /** Fails if `actual` and `expected` are structurally equal (see assertEqual). */
  export function assertNotEqual<T>(actual: T, expected: T, message?: string | null): void;
  /** Fails unless |actual - expected| <= tolerance (default 1e-9). */
  export function assertClose(actual: number, expected: number, tolerance?: number, message?: string | null): void;
  /** Fails unless calling `fn` raises a runtime error (throw, abort, failed assertion, index out
   *  of range, ...) whose message contains `expectedMessage` (when given). Budget, memory and stack
   *  exhaustion are not caught. */
  export function assertThrows(fn: () => void, expectedMessage?: string | null): void;
  /** Fails the test unconditionally. */
  export function fail(message?: string | null): never;
}
