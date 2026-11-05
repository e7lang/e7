// @runtime-error: assertEqual failed: sums
// @runtime-error: expected: [ 1, 2, 4 ]
// @runtime-error: actual:   [ 1, 2, 3 ]
// @runtime-error: rt_assert_equal.ts:6:1)
import { assertEqual } from "e7:test";
assertEqual([1, 2, 3], [1, 2, 4], "sums");
