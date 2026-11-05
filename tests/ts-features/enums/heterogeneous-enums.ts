// @feature: Heterogeneous enums (numbers and strings mixed)
// @category: Enums
// @status: unsupported
// @why: types known at compile time: an enum is either `number` or `string`
// @instead: two enums, or string values only
// @error: heterogeneous enums are not supported
enum BooleanLikeHeterogeneousEnum { No = 0, Yes = "YES" }
console.log(BooleanLikeHeterogeneousEnum.Yes);
