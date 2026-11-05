// @feature: Equality narrowing (`===`, `!==`, `== null`)
// @category: Narrowing
// @status: supported
function f(x: string | undefined, y: string | null): void {
  if (x !== undefined && y != null) {
    console.log(x.toUpperCase(), y.toLowerCase());
  } else if (x == null) {
    console.log("x missing");
  } else {
    console.log("y missing", x.length);
  }
}
f("a", "B");
f(undefined, "B");
f("abc", null);
