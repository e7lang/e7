// a value narrowed to never (a number cast to an enum it is not a member of) keeps its value
enum E { X = 10, Y = 20 }
function assertNever(x: never): string { return "unexpected " + x + "|" + String(x) + `|${x}`; }
function name(x: E): string {
  switch (x) {
    case E.X: return "X";
    case E.Y: return "Y";
    default: return assertNever(x);
  }
}
function show(x: never): void { console.log(x, JSON.stringify(x), [x]); }
function f(x: E): void {
  switch (x) {
    case E.X: case E.Y: return;
    default: show(x);
  }
}
const wire = Uint8Array.of(10, 30);
console.log(name(wire[0] as E), name(wire[1] as E));
f(wire[1] as E);
