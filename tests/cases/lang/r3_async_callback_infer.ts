// third test round of 0.1.3 (async_callback_infer): must print what Node.js prints
function call<T>(fn: (n: number) => T): T { return fn(2); }
console.log(call((n) => n * 2));
function callP<T>(fn: (n: number) => Promise<T>): Promise<T> { return fn(2); }
callP((n) => Promise.resolve(n * 3)).then((v) => console.log(v));
callP(async (n) => n * 4).then((v) => console.log(v));
