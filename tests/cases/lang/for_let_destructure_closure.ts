// for (let [i] = ...) gives each iteration its own binding, like for (let i = ...)
const fs: (() => number)[] = [];
for (let [i] = [0]; i < 3; i++) fs.push(() => i);
console.log(fs.map((f) => f()));
const gs: (() => number)[] = [];
for (let { n } = { n: 10 }; n < 13; n++) gs.push(() => n);
console.log(gs.map((g) => g()));
