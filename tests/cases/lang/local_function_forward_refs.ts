// const function values of a block calling the ones declared after them (mutual recursion); calling one
// before its declaration ran is a ReferenceError
function check(n: number): boolean {
  const isEven = (k: number): boolean => k === 0 ? true : isOdd(k - 1);
  const isOdd = (k: number): boolean => k === 0 ? false : isEven(k - 1);
  return isEven(n);
}
console.log(check(10), check(7));
{
  const a = (): number => b() + 1;
  const b = (): number => 1;
  console.log(a());
}
function early(): void {
  const f = (): number => g() + 1;
  try { console.log(f()); } catch (e) { console.log("caught", e instanceof ReferenceError, String(e)); }
  const g = (): number => 1;
  console.log(f());
}
early();
try { throw new ReferenceError("r"); } catch (e) { console.log(e instanceof ReferenceError, e instanceof TypeError, String(e)); }
function dflt(x: number | undefined = 1, y: number): number { return (x ?? 0) + y; }
console.log(dflt(undefined, 2), dflt(5, 2));
