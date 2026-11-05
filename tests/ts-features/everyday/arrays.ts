// @feature: Array types `T[]` and `Array<T>`
// @category: Everyday Types
// @status: supported
const xs: number[] = [3, 1, 2];
const names: Array<string> = ["b", "a"];
xs.push(4);
names.sort();
console.log(xs, names, xs.length, xs[0], names.indexOf("b"));
