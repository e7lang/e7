// fourth test round of 0.1.3 (scope/switch_case_fundecl): must print what Node.js prints
function sw(n: number): string {
  switch (n) {
    case 1:
      const b = "one";
      function f1(): string { return b; }
      return f1();
    default:
      return "def";
  }
}
console.log(sw(1), sw(2));
