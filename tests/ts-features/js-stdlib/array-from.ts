// @feature: `Array.from` and `Array.of`
// @category: JavaScript: Standard Library
// @status: supported
console.log(Array.from({ length: 4 }, (_, i) => i * i), Array.from(new Set([1, 1, 2])), Array.from("hé"), Array.of("a", "b"));
