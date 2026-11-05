// Dates are objects: Map and Set compare them by identity, like every other object
const a = new Date(0);
const b = new Date(0);
const m = new Map<Date, string>();
m.set(a, "a");
m.set(b, "b");
console.log(m.size, m.get(a), m.get(b), m.has(new Date(0)));
const s = new Set<Date>([a, b, a]);
console.log(s.size, s.has(a), s.has(new Date(0)));
