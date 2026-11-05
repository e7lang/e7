// @runtime-error: TypeError: not handled here
function f(): void {
  try {
    throw new TypeError("not handled here");
  } catch (e) {
    if (e instanceof RangeError) console.log("range");
    else throw e;
  }
}
f();
