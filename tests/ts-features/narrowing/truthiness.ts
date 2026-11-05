// @feature: Truthiness narrowing
// @category: Narrowing
// @status: supported
function printAll(strs: string[] | null): void {
  if (strs && strs.length > 0) {
    for (const s of strs) console.log(s);
  } else {
    console.log("(none)");
  }
}
printAll(["a", "b"]);
printAll(null);
printAll([]);
const name: string | undefined = "";
const zero = 0, text = "0";
console.log(name ? "set" : "empty", !!zero, !!text, !!NaN);
