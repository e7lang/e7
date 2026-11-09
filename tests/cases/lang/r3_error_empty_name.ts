// third test round of 0.1.3 (error_empty_name): must print what Node.js prints
const m = new Error("msg");
m.name = "";
console.log(String(m), `${m}`);
