// Node.js shim so that E7 test programs can run unmodified on Node (differential testing).
// Usage: node --experimental-transform-types --import ./tests/node_shim/register.mjs file.ts
import { register } from "node:module";
import { pathToFileURL } from "node:url";

register("./hooks.mjs", import.meta.url);

const g = globalThis;
const big = (x) => (typeof x === "bigint" ? x : BigInt(Math.trunc(Number(x))));
g.u32 = (x) => (typeof x === "bigint" ? Number(BigInt.asUintN(32, x)) : typeof x === "boolean" ? +x : x >>> 0);
g.i32 = (x) => (typeof x === "bigint" ? Number(BigInt.asIntN(32, x)) : typeof x === "boolean" ? +x : x | 0);
g.u16 = (x) => g.u32(x) & 0xffff;
g.i16 = (x) => (g.u32(x) << 16) >> 16;
g.u8 = (x) => g.u32(x) & 0xff;
g.i8 = (x) => (g.u32(x) << 24) >> 24;
g.i64 = (x) => Number(BigInt.asIntN(64, big(x)));
g.u64 = (x) => Number(BigInt.asUintN(64, big(x)));
g.int = g.i64;
g.rotl32 = (x, n) => { n &= 31; return ((x << n) | (x >>> (32 - n))) >>> 0; };
g.rotr32 = (x, n) => { n &= 31; return ((x >>> n) | (x << (32 - n))) >>> 0; };
g.clz32 = (x) => Math.clz32(x);
g.ctz32 = (x) => (x === 0 ? 32 : 31 - Math.clz32(x & -x));
g.popcnt = (x) => { let c = 0; x = x >>> 0; while (x) { x &= x - 1; c++; } return c; };
g.bswap32 = (x) => (((x & 0xff) << 24) | ((x & 0xff00) << 8) | ((x >>> 8) & 0xff00) | (x >>> 24)) >>> 0;
g.bswap16 = (x) => ((x & 0xff) << 8) | ((x >>> 8) & 0xff);
g.idiv = (a, b) => Math.trunc(a / b);
g.imod = (a, b) => a % b;
g.print = (...a) => console.log(...a);
g.abort = (m) => { throw new Error(m ?? "abort"); };
