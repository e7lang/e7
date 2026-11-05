// @runtime-error: index 5 out of range
// e7-specific errors (here: an out-of-bounds read) end the call without running finally blocks
const xs = [1, 2];
try {
  console.log(xs[5]);
} finally {
  console.log("not printed");
}
