// @feature: Exhaustiveness checking (`never`, exhaustive `switch`)
// @category: Narrowing
// @status: partial
// @why: not implemented yet: the `default` branch of a `switch` over every enum member does not see the value as `never`
// @instead: leave out the `default` branch (the `switch` is exhaustive), or make it throw
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
