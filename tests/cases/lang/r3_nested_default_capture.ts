// third test round of 0.1.3 (nested_default_capture): must print what Node.js prints
function outer(): number {
  const k = 1;
  function h(n: number = 4): number { return n + k; }
  return h(2);
}
console.log(outer());
