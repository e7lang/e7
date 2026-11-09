// third test round of 0.1.3 (default_param_not_narrowed): must print what Node.js prints
function f(a: number | undefined = 9): number { return a - 1; }
console.log(f(), f(undefined), f(3));
