// third test round of 0.1.3 (cond_optional_prop): must print what Node.js prints
interface O { error?: string; n?: number }
const flag = Math.PI > 3;
const o: O = {};
o.error = flag ? "a" : "b";
const lit: O = { n: flag ? 1 : 2 };
console.log(o, lit);
