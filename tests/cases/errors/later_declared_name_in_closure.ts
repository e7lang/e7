// @error: is used before its declaration later in this block
// the closure reads the 'z' of its block (declared below), not the outer one
function t(): void {
  const z = "outer";
  {
    function f(): string { return z; }
    const z = "inner";
    console.log(f());
  }
}
t();
