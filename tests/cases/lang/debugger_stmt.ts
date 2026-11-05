// `debugger;` is a no-op when no debugger is attached (also in Node.js)
function f(x: number): number {
  debugger;
  return x * 2;
}
debugger;
console.log(f(21));
