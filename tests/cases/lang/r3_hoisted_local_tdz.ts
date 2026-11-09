// third test round of 0.1.3 (hoisted_local_tdz): must print what Node.js prints
function t1(): void {
  try { console.log(q()); } catch (e) { console.log(String(e)); }
  const z = 5;
  function q(): number { return z; }
}
t1();
function t2(): void {
  try { console.log(r()); } catch (e) { console.log(String(e)); }
  const s = "str";
  function r(): number { return s.length; }
}
t2();
