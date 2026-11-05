// @runtime-error: test failed: unreachable branch reached
// @runtime-error: rt_test_fail.ts:5:14)
import { fail } from "e7:test";
const n: number = [1, 2].length;
if (n === 2) fail("unreachable branch reached");
