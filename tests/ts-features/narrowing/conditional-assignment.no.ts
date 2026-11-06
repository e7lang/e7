// @feature: Narrowing after a conditional assignment (`if (x === null) x = v; use(x)`)
// @category: Narrowing
// @status: partial
// @why: not implemented yet: a conditional expression `x = x === null ? v : x` does not narrow `x`
// @instead: `x ??= v`, or `if (x === null) x = v;`
// @error: type 'number[] | null' is not assignable to type 'number[]'
function ensure(list: number[] | null): number[] {
  list = list === null ? [] : list;
  return list;
}
console.log(ensure(null));
