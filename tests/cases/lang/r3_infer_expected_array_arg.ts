// third test round of 0.1.3 (infer_expected_array_arg): must print what Node.js prints
type Dir = "n" | "s";
function first<T>(xs: T[]): T { return xs[0]; }
const d: Dir = first(["s", "n"]);
console.log(d);
