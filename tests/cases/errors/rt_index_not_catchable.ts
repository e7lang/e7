// @runtime-error: index 5 out of range
// array bounds are an E7 check (Node.js returns undefined), so try/catch does not catch them
const xs: number[] = [1, 2, 3];
try {
  console.log(xs[5]);
} catch (e) {
  console.log("caught");
}
