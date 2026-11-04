// @error: explicit return type
function f(n: number) { return n <= 0 ? 0 : f(n - 1); }
