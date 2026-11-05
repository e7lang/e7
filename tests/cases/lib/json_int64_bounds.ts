// E7-only: 2^63 / 2^64 are out of range for i64 / u64 (they are not representable below the limit as doubles)
import { tryParse, lastError } from "e7:json";
interface U { v: u64 }
interface I { v: i64 }
console.log(tryParse('{"v":18446744073709551616}') as U | null, lastError());
console.log(tryParse('{"v":9223372036854775808}') as I | null, lastError());
const ok = tryParse('{"v":18446744073709549568}') as U | null;   // largest double below 2^64
console.log(ok === null ? "null" : ok.v.toString());
const okI = tryParse('{"v":-9223372036854775808}') as I | null;
console.log(okI === null ? "null" : okI.v.toString());
const big = tryParse('{"v":9223372036854774784}') as I | null;
console.log(big === null ? "null" : big.v.toString());
