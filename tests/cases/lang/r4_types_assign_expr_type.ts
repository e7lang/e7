// fourth test round of 0.1.3 (types/assign_expr_type): must print what Node.js prints
function g(s: string): number { return s.length; }
let y: string | null = null;
console.log(g((y = "abc")), y);
