// @error: lacks ending return statement
enum Op { Add, Sub, Mul }
function apply(op: Op, a: number, b: number): number {
  switch (op) {
    case Op.Add: return a + b;
    case Op.Sub: return a - b;
  }
}
console.log(apply(Op.Add, 1, 2));
