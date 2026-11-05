// @feature: `null` and `undefined` with `strictNullChecks`
// @category: Everyday Types
// @status: supported
function doSomething(x: string | null): void {
  if (x === null) {
    console.log("nothing");
  } else {
    console.log("Hello, " + x.toUpperCase());
  }
}
doSomething(null);
doSomething("ts");
const u: undefined = undefined;
console.log(u, null, u === undefined);
