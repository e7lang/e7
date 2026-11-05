// @feature: `for...of` over arrays, `entries()`, `keys()`, `values()`
// @category: Iterators and Generators
// @status: supported
// @note: an array's `entries()` / `keys()` / `values()` are used directly in `for...of` (they are not iterator objects)
const xs = ["a", "b", "c"];
for (const x of xs) console.log(x);
for (const [i, x] of xs.entries()) console.log(i, x);
for (const i of xs.keys()) console.log(i);
for (const v of xs.values()) console.log(v);
