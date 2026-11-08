// @error: a promise callback cannot have a rest parameter
function L(...a: string[]): void { console.log(a.length); }
Promise.resolve("x").then(L);
