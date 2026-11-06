// `export default <name>` of a function declared later in the module (functions are hoisted)
export default describe;
function describe(n: number): string {
  return n % 2 === 0 ? "even" : "odd";
}
