// fourth test round of 0.1.3 (mixed/clone_error_name): must print what Node.js prints
const a = new Error("boom");
a.name = "Custom";
const c = structuredClone(a);
console.log(c.name, c.message, String(c), c instanceof Error);
const d = structuredClone({ e: a });
console.log(String(d.e), d.e.name);
const r = new RangeError("r");
r.name = "Mine";
const rc = structuredClone(r);
console.log(String(rc), rc instanceof RangeError);
const t = new Error("t");
t.name = "TypeError";
const tc = structuredClone(t);
console.log(String(tc), tc instanceof TypeError);
