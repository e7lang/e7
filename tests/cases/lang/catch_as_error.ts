// catch (e) { (e as Error).message }: a caught value is always an Error in E7 (only Errors can be thrown)
function parse(s: string): number {
  const n = Number(s);
  if (Number.isNaN(n)) throw new RangeError(`not a number: ${s}`);
  return n;
}
for (const s of ["12", "x"]) {
  try {
    console.log(parse(s));
  } catch (e) {
    const err = e as Error;
    console.log(err.name, (e as Error).message, (e as RangeError).name, String(e));
  }
}
try {
  JSON.parse("{") as number;
} catch (e) {
  console.log((e as SyntaxError).name, (e as Error) instanceof SyntaxError);
}
