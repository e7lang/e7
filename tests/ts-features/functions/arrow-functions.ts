// @feature: Arrow functions and closures
// @category: More on Functions
// @status: supported
function counter(): () => number {
  let n = 0;
  return () => ++n;
}
const c = counter();
c();
c();
const add = (a: number) => (b: number) => a + b;
console.log(c(), add(2)(3), [1, 2, 3].reduce((s, x) => s + x, 0));
