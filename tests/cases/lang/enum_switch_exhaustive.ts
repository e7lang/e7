// a switch with a case for every member of an enum is exhaustive (as in tsc): no default or final
// return is needed; cases may share a body
enum Op { Add, Sub, Mul = 10 }
enum Unit { Ms = "ms", S = "s" }
function apply(op: Op, a: number, b: number): number {
  switch (op) {
    case Op.Add: return a + b;
    case Op.Sub: return a - b;
    case Op.Mul: return a * b;
  }
}
function scale(u: Unit): number {
  switch (u) {
    case Unit.Ms: return 1;
    case Unit.S: return 1000;
  }
}
function kind(op: Op): string {
  switch (op) {
    case Op.Add:
    case Op.Sub:
      return "additive";
    case Op.Mul: {
      const s = "multiplicative";
      return s;
    }
  }
}
console.log(apply(Op.Add, 2, 3), apply(Op.Sub, 2, 3), apply(Op.Mul, 2, 3), scale(Unit.S), kind(Op.Sub), kind(Op.Mul));
