const xs = [1, 4, 9];
console.log(xs.map(Math.sqrt), ["10", "10", "10"].map(parseInt), ["1", "2.5"].map(Number), [0, 1, 2].map(String));
console.log(["a", "", "b"].filter(Boolean), [1.5, 2.5].map(Math.round), [2, 3].map(Math.pow));
function scale(x: number, factor = 10): number { return x * factor; }
console.log(xs.map(scale));
const f: (s: string) => number = parseFloat;
console.log(f("3.25"), [-1, 2].map(Math.abs), [1.1].some(Number.isInteger));
