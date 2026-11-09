// fourth test round of 0.1.3 (scope/switch_case_capture): must print what Node.js prints
function sw(n: number): string {
  switch (n) {
    case 2:
      const c = "two";
      const g = () => c;
      return g();
  }
  return "x";
}
console.log(sw(2));
