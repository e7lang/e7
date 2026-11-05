// @feature: Immediately invoked function expressions
// @category: More on Functions
// @status: supported
const config = (() => {
  const base = 10;
  return { base, double: base * 2 };
})();
console.log(config, ((x: number) => x + 1)(41));
