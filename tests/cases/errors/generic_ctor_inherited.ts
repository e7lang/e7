// @error: cannot infer the type arguments of 'Sub' (its constructor is inherited)
class Base {
  constructor(public v: number) {}
}
class Sub<T> extends Base {
  extra: T[] = [];
}
console.log(new Sub(1).v);
