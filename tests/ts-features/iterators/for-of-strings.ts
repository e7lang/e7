// @feature: `for...of` over strings (by code point)
// @category: Iterators and Generators
// @status: supported
const chars: string[] = [];
for (const ch of "héllo 😀!") chars.push(ch);
console.log(chars, chars.length, "😀".length, [..."ab"]);
