// @feature: Default parameters
// @category: More on Functions
// @status: supported
// @note: a default may refer to earlier parameters (in declarations, methods, constructors and arrow functions)
const STEP = 1;
function range(start: number, end: number, step = STEP, label = "range"): string {
  const out: number[] = [];
  for (let i = start; i < end; i += step) out.push(i);
  return label + ": " + out.join(",");
}
function f(a: number, b = a + 1): number {
  return a + b;
}
const mk = (prefix = "#") => (n: number) => prefix + n;
const span = (start: number, end = start + 10) => `${start}-${end}`;
class Greeter {
  greet(name = "world", times = name.length): string { return "hello " + name + " x" + times; }
}
console.log(range(0, 5), range(0, 10, 3), range(1, 3, 1, "r"), f(1), f(1, 5), mk()(1), mk("$")(2), span(1), span(1, 2), new Greeter().greet());
