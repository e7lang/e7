// @error: object is possibly 'undefined'
// a nested function may assign undefined: the narrowing before the loop does not hold after it
function f(m: Map<string, string>): number {
  let v = m.get("k");
  if (v === undefined) v = "fb";
  for (let i = 0; i < 2; i++) { const g = (): void => { v = undefined; }; g(); }
  return v.length;
}
console.log(f(new Map()));
