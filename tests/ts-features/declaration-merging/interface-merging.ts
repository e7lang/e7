// @feature: Merging interfaces
// @category: Declaration Merging
// @status: supported
// @note: two `interface` declarations of one name in one scope merge their members
interface Box { height: number; width: number }
interface Box { scale: number }
const box: Box = { height: 5, width: 6, scale: 10 };
console.log(box);
