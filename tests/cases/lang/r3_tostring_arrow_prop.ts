// third test round of 0.1.3 (tostring_arrow_prop): must print what Node.js prints
const o = { toString: () => "T" };
const p = { toString() { return "M"; } };
console.log([o].join(), String(o), `${o}`, "" + o, [p].join(), String(p));
const q = { toString: function (): string { return "G"; } };
console.log(`${q}`);
