// @error: a void value cannot be passed as an argument
function take<T>(x: T): number { return 1; }
function g(): void {}
console.log(take(g()));
