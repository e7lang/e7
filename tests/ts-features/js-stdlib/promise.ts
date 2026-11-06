// @feature: `Promise`
// @category: JavaScript: Standard Library
// @status: supported
// @note: `then` / `catch` / `finally`, `new Promise`, `Promise.resolve` / `reject` / `all` / `allSettled` / `any` / `race` over promises of one type; callbacks run in the order of Node.js (the VM implements the promise jobs); the host is the event loop (no timers)
Promise.resolve(42).then(v => console.log(v));
const p = new Promise<string>((resolve) => resolve("ok"));
p.then(s => s.length).then(n => console.log(n)).finally(() => console.log("done"));
Promise.all([Promise.resolve(1), Promise.resolve(2)]).then(xs => console.log(xs));
Promise.reject(new Error("no")).catch(e => console.log("caught", e.message));
