// @runtime-error: expected 1 | 2 at $[1]
enum Lv { Low = 1, High = 2 }
const xs = JSON.parse(`[1, 3]`) as Lv[];
console.log(xs.length);
