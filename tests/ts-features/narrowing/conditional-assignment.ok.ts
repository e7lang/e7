// @feature: Narrowing after a conditional assignment (`if (x === null) x = v; use(x)`)
// @category: Narrowing
// @status: partial
// @note: an `if` branch (or both branches) ending in an assignment of a non-null value
function ensure(list: number[] | null): number[] {
  if (list === null) list = [];
  list.push(1);
  return list;
}
function pick(name: string | undefined, short: boolean): string {
  if (short) name = "s";
  else name = "long";
  return name;
}
console.log(ensure(null), ensure([0]), pick(undefined, true));
