// @feature: Optional parameters
// @category: More on Functions
// @status: supported
function fixed(n: number, digits?: number): string {
  return n.toFixed(digits ?? 1);
}
console.log(fixed(3.14159), fixed(3.14159, 3));
