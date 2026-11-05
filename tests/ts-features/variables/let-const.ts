// @feature: `let` and `const` with block scoping
// @category: Variable Declaration
// @status: supported
const a = 1;
let b = 2;
{
  const a = 10;
  let b = 20;
  b += a;
  console.log(a, b);
}
if (true) {
  let c = b * 2;
  console.log(c);
}
b++;
console.log(a, b);
