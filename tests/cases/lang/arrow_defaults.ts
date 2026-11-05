// Default parameter values in arrow functions and function expressions: the default applies when
// the argument is omitted or undefined, and may refer to earlier parameters.

const add = (a: number, b = 10): number => a + b;
console.log(add(1), add(1, 2), add(1, undefined));

const greet = (name: string, greeting = "Hello", punct = greeting === "Hi" ? "!" : "."): string =>
  `${greeting}, ${name}${punct}`;
console.log(greet("Ann"), greet("Bob", "Hi"), greet("Cy", "Yo", "?"));

// inferred types of defaults (number, string, boolean, array, object)
const opts = (n = 1, s = "x", flag = false, xs = [1, 2], o = { k: 3 }) => `${n} ${s} ${flag} ${xs.length} ${o.k}`;
console.log(opts(), opts(2, "y", true, [5], { k: 9 }));

// a default that refers to earlier parameters
const range = (start: number, end = start + 3, step = 1): number[] => {
  const out: number[] = [];
  for (let i = start; i < end; i += step) out.push(i);
  return out;
};
console.log(range(2), range(0, 10, 4));

// annotated with a default, and contextual types
const scale: (x: number, f?: number) => number = (x, f = 2) => x * f;
console.log(scale(4), scale(4, 3));
const fixed: (x: number, f: number) => number = (x, f = 2) => x * f;
console.log(fixed(4, 5));

// defaults are evaluated on every call
let calls = 0;
const next = (): number => ++calls;
const withCounter = (v = next()): number => v;
console.log(withCounter(), withCounter(), withCounter(100), calls);

// a nullable parameter type: null is passed through, only undefined uses the default
const orNull = (v: string | null = "default"): string => (v === null ? "null" : v);
console.log(orNull(), orNull(null), orNull("given"), orNull(undefined));

// function expressions
const fe = function (a: number, b: number = a * 2): number {
  return a + b;
};
console.log(fe(1), fe(1, 1));

// closures capturing a parameter with a default
const makeAdder = (base = 100) => (x: number) => x + base;
console.log(makeAdder()(1), makeAdder(5)(1));
const counters = [1, 2, 3].map((x, i = 0) => () => x * 10 + i);
console.log(counters.map((f) => f()));

// callbacks with defaults
console.log([1, 2, 3].map((x: number, i = 0) => x + i));
const values = ["a", "b"].map((s, i, arr = []) => s + i + arr.length);
console.log(values);

// destructuring parameter with a default
const point = ({ x, y }: { x: number; y: number } = { x: 1, y: 2 }) => x + y;
console.log(point(), point({ x: 10, y: 20 }));
