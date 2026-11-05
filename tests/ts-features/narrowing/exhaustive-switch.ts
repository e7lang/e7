// @feature: Exhaustiveness checking (`never`, exhaustive `switch`)
// @category: Narrowing
// @status: unsupported
// @why: not implemented yet: a `switch` does not narrow an enum value to the remaining members (so the `default` branch does not see `never`, and a `switch` over every member is not known to be exhaustive)
// @instead: a `default` branch that throws, or a final `return` / `throw` after the `switch`
// @error: is not assignable to type 'never'
enum Dir { Up, Down }
function arrow(d: Dir): string {
  switch (d) {
    case Dir.Up: return "^";
    case Dir.Down: return "v";
    default: {
      const unreachable: never = d;
      return unreachable;
    }
  }
}
console.log(arrow(Dir.Up));
