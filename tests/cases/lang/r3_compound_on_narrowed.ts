// third test round of 0.1.3 (compound_on_narrowed): must print what Node.js prints
function f(n: number | null, s: string | undefined): void {
  if (n === null || s === undefined) return;
  n += 1;   // error: operator '+=' cannot be applied to types 'number | null' and 'number'
  s += "!";
  console.log(n, s);
}
f(1, "a");
let c: number | undefined;
c ??= 0;
c++;      // error: operator '++' requires a number or integer operand
console.log(c);
