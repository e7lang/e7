// Integer-like keys come first, in ascending order (Object.keys, JSON.stringify, console.log)
const o = { b: 1, "10": "ten", a: 2, "2": "two", "01": "not an index" };
console.log(Object.keys(o).join(","));
console.log(JSON.stringify(o));
console.log(JSON.stringify(o, null, 1));
console.log(o);
