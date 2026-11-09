// fourth test round of 0.1.3 (scope/t04_switch_scope): must print what Node.js prints
function sw(n: number): string {
  const out: string[] = [];
  switch (n) {
    case 1:
      let x = 1;
      out.push("c1:" + x);
    // falls through
    case 2:
      x = 5;
      out.push("c2:" + x);
      break;
    default:
      out.push("d");
  }
  return out.join(",");
}
console.log(sw(1));
try { console.log(sw(2)); } catch (e) { console.log(String(e)); }
console.log(sw(3));
function sw3(n: number): number {
  switch (n) {
    case 1:
      let y = 10;
      y++;
      return y;
    case 2:
      const z = 20;
      return z;
  }
  return 0;
}
console.log(sw3(1), sw3(2), sw3(3));
