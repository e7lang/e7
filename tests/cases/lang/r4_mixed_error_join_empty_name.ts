// fourth test round of 0.1.3 (mixed/error_join_empty_name): must print what Node.js prints
const e = new Error("msg");
e.name = "";
console.log(String(e), `${e}`, [e].join(","), String([e]), `${[e]}`);
