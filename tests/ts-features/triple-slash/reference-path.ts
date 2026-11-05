// @feature: `/// <reference path="..." />`
// @category: Triple-Slash Directives
// @status: supported
// @note: a comment for E7; it lets tsc and IDEs see a declaration file such as `lib/e7.d.ts`
/// <reference path="../../../lib/e7.d.ts" />
const x: u32 = u32(0xffffffff + 2);
console.log(x, rotl32(u32(1), 4), clz32(u32(1)));
