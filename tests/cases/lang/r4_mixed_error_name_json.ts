// fourth test round of 0.1.3 (mixed/error_name_json): must print what Node.js prints
const a = new Error("boom");
a.name = "ValidationError";
console.log(JSON.stringify(a));
const b = new Error();
b.message = "late";
console.log(JSON.stringify(b));
const c = new TypeError("t");
c.message = "changed";
console.log(JSON.stringify(c));
console.log(JSON.stringify({ err: a, n: 1 }));
console.log(JSON.stringify([a, b]));
