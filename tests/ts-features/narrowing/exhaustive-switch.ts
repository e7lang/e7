// @feature: Exhaustiveness checking (`never`, exhaustive `switch`)
// @category: Narrowing
// @status: supported
// @note: a `switch` with a case for every enum member or literal is exhaustive; its `default` branch sees the value as `never`
enum Dir { Up, Down }
type Mode = "r" | "w";
function assertNever(x: never): never { throw new Error("unexpected value"); }
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
function flag(m: Mode): number {
  switch (m) {
    case "r": return 4;
    case "w": return 2;
    default: return assertNever(m);
  }
}
console.log(arrow(Dir.Up), arrow(Dir.Down), flag("w"));
