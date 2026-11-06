// @feature: Exhaustiveness checking (`never`, exhaustive `switch`)
// @category: Narrowing
// @status: partial
// @note: a `switch` with a case for every enum member is exhaustive: no `default` or final `return` is needed
enum Dir { Up, Down }
enum Mode { Read = "r", Write = "w" }
function arrow(d: Dir): string {
  switch (d) {
    case Dir.Up: return "^";
    case Dir.Down: return "v";
  }
}
function flag(m: Mode): number {
  switch (m) {
    case Mode.Read:
    case Mode.Write:
      return m === Mode.Read ? 4 : 2;
  }
}
console.log(arrow(Dir.Up), arrow(Dir.Down), flag(Mode.Write));
