// @feature: Narrowing after a conditional assignment (`if (x === null) x = v; use(x)`)
// @category: Narrowing
// @status: supported
// @note: an `if` branch (or both) ending in an assignment of a non-null value, a conditional expression with non-null branches, and static field paths
function ensure(list: number[] | null): number[] {
  if (list === null) list = [];
  list.push(1);
  return list;
}
function viaTernary(list: number[] | null): number[] {
  list = list === null ? [] : list;
  return list;
}
class Single {
  static #it: Single | null = null;
  static get(): Single {
    if (Single.#it === null) Single.#it = new Single();
    return Single.#it;
  }
}
console.log(ensure(null), viaTernary(null), Single.get() === Single.get());
