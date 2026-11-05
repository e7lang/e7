// @feature: `Array` methods
// @category: JavaScript: Standard Library
// @status: partial
// @note: ES2022 methods (`map filter reduce find findLast flat flatMap at includes splice sort` ...)
const a = [5, 1, 4, 2, 3];
console.log(a.map(x => x * 2), a.filter(x => x % 2 === 1), a.reduce((s, x) => s + x, 0), a.find(x => x > 3), a.findLast(x => x > 3));
console.log(a.findIndex(x => x === 4), a.some(x => x > 4), a.every(x => x > 0), a.includes(3), a.indexOf(9), a.at(-1), a.join("|"));
const b = [...a];
b.sort();
const c = [...a].sort((x, y) => y - x);
const removed = c.splice(1, 2, 99);
console.log(b, c, removed, [[1, 2], [3]].flat(), [[[1], [2]], [[3]]].flat(Infinity), [1, 2].flatMap(x => [x, x * 10]));
console.log(a.slice(1, 3), a.concat([6]), [1, 2, 3].reverse(), new Array<number>(3).fill(7), Array.of(1, 2), a.reduceRight((s, x) => s + x, ""));
const d = [1, 2, 3, 4];
d.length = 2;
d.unshift(0);
console.log(d, d.shift(), d.pop(), d);
