// @feature: Function type expressions `(a: A) => R`
// @category: More on Functions
// @status: supported
function greeter(fn: (a: string) => void): void {
  fn("Hello, World");
}
function printToConsole(s: string): void {
  console.log(s);
}
greeter(printToConsole);
type BinOp = (a: number, b: number) => number;
const ops: BinOp[] = [(a, b) => a + b, (a, b) => a * b];
console.log(ops.map(op => op(3, 4)));
