// @feature: Function compatibility with fewer parameters
// @category: Type Compatibility and Inference
// @status: partial
// @note: a function literal written where a function type is expected may declare fewer parameters; parameter and return types must match
const y: (b: number, s: string) => number = b => b * 2;
const items = [1, 2, 3];
items.forEach(item => console.log(item));
console.log(y(1, "a"), items.map((v, i, arr) => v + i + arr.length), items.map(() => 0));
