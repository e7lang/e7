// narrowing by an assignment that ends an if branch survives the if when the other path is narrowed
// to the same type by the condition (or assigns too), as in tsc
function ensure(list: number[] | null): number[] {
  if (list === null) list = [];
  list.push(1);
  return list;
}
function ensureBlock(list: string[] | undefined): number {
  if (list === undefined) {
    const fresh: string[] = ["x"];
    list = fresh;
  }
  return list.length;
}
function both(x: string | undefined, short: boolean): string {
  if (short) x = "s";
  else x = "long";
  return x.toUpperCase();
}
function viaElse(x: number | null): number {
  if (x !== null) {
    x = x + 1;
  } else {
    x = 0;
  }
  return x * 2;
}
function coalesce(x: number[] | null, add: boolean): number {
  if (add) x ??= [7];
  else x ??= [];
  return x.length;
}
console.log(ensure(null), ensure([0]), ensureBlock(undefined), ensureBlock(["a", "b"]), both(undefined, true), both("q", false));
console.log(viaElse(null), viaElse(4), coalesce(null, true), coalesce([1, 2], false));

// a conditional expression whose branches are both non-null narrows the target; so does an assignment to a
// static field path (also #private)
function viaTernary(list: number[] | null): number[] {
  list = list === null ? [] : list;
  list.push(2);
  return list;
}
class Registry {
  static #instance: Registry | null = null;
  static last: string | undefined;
  count = 0;
  static get(): Registry {
    if (Registry.#instance === null) Registry.#instance = new Registry();
    Registry.#instance.count++;
    return Registry.#instance;
  }
  static label(): string {
    Registry.last ??= "none";
    return Registry.last.toUpperCase();
  }
}
console.log(viaTernary(null), viaTernary([1]), Registry.get().count, Registry.get().count, Registry.label());
