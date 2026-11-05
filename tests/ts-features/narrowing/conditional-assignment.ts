// @feature: Narrowing after a conditional assignment (`if (x === null) x = v; use(x)`)
// @category: Narrowing
// @status: unsupported
// @why: not implemented yet: narrowing by assignment does not survive the end of an `if` branch (nor a conditional expression `x = x === null ? v : x`)
// @instead: `x ??= v`, or a new `const` (`const y = x === null ? v : x`), or return early
// @error: type 'number[] | null' is not assignable to type 'number[]'
function ensure(list: number[] | null): number[] {
  if (list === null) list = [];
  list.push(1);
  return list;
}
console.log(ensure(null), ensure([0]));
