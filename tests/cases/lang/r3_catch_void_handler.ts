// third test round of 0.1.3 (catch_void_handler): must print what Node.js prints
const p: Promise<string> = Promise.reject(new Error("boom"));
p.catch((e: Error) => { console.log("caught", e.message); });
