// @feature: Contextual typing of callbacks and object literals
// @category: Type Compatibility and Inference
// @status: supported
interface Handler { name: string; run: (n: number) => string }
const handlers: Handler[] = [
  { name: "hex", run: n => n.toString(16) },
  { name: "bin", run: n => n.toString(2) },
];
const sorted = [3, 1, 2].sort((a, b) => b - a);
console.log(handlers.map(h => `${h.name}:${h.run(10)}`), sorted);
