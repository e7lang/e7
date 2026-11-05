// @feature: The `unknown` type
// @category: Everyday Types
// @status: partial
// @note: only as the type of a `catch` variable (narrowed with `instanceof`) and of an ignored callback parameter
try {
  throw new RangeError("out of range");
} catch (e: unknown) {
  if (e instanceof RangeError) console.log(e.name, e.message);
}
console.log(Array.from({ length: 3 }, (_: unknown, i) => i * i));
