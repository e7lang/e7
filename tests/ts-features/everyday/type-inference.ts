// @feature: Type inference for variables and return values
// @category: Everyday Types
// @status: supported
let count = 1;
const label = "items";
function describe(n: number) {
  return `${n} ${label}`;
}
count += 2;
const doubled = [1, 2, 3].map(x => x * 2);
console.log(describe(count), doubled);
