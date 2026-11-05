// @feature: Union types
// @category: Everyday Types
// @status: partial
// @note: unions with `null` / `undefined` (`T | null | undefined`)
function lengthOf(s: string | null | undefined): number {
  if (s == null) return -1;
  return s.length;
}
let maybe: number | undefined;
console.log(lengthOf("abc"), lengthOf(null), lengthOf(undefined), maybe);
maybe = 4;
console.log(maybe + 1);
