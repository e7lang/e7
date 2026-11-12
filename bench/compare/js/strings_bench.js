const log = typeof print === "function" ? print : console.log;
const t0 = Date.now();
const parts = [];
for (let i = 0; i < 200000; i++) parts.push(`k${i}=${(i * 7919) % 1000}`);
const joined = parts.join("&");
const fields = joined.split("&");
const m = new Map();
for (const f of fields) { const eq = f.indexOf("="); m.set(f.slice(0, eq), f.slice(eq + 1)); }
const keys = [...m.keys()].sort();
let acc = 0;
for (const k of keys) acc += m.get(k).length;
log(fields.length, acc, "strings", Date.now() - t0, "ms");
