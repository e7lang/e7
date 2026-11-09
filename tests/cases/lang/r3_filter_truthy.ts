// third test round of 0.1.3 (filter_truthy): must print what Node.js prints
const xs = ["a", "", "b"];
console.log(xs.filter(Boolean));
console.log(xs.filter((x) => x));
const ns = [0, 1, 2, NaN];
console.log(ns.filter((n) => n));
const os: ({ a: number } | null)[] = [{ a: 1 }, null];
console.log(os.filter((o) => o));
