// @feature: Default parameters
// @category: More on Functions
// @status: partial
// @note: defaults in function declarations, methods and constructors; in arrow functions and function expressions they may refer to earlier parameters
const STEP = 1;
function range(start: number, end: number, step = STEP, label = "range"): string {
  const out: number[] = [];
  for (let i = start; i < end; i += step) out.push(i);
  return label + ": " + out.join(",");
}
const mk = (prefix = "#") => (n: number) => prefix + n;
const span = (start: number, end = start + 10) => `${start}-${end}`;
class Greeter {
  greet(name = "world"): string { return "hello " + name; }
}
console.log(range(0, 5), range(0, 10, 3), range(1, 3, 1, "r"), mk()(1), mk("$")(2), span(1), span(1, 2), new Greeter().greet());
