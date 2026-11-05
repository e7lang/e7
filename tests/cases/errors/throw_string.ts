// @error: throwing a string is not supported; throw new Error(message) instead
function f(): void { throw "boom"; }
f();
