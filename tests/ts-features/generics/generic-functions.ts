// @feature: Generic functions
// @category: Generics
// @status: supported
// @note: compiled by monomorphization (one copy per list of type arguments)
function identity<T>(arg: T): T {
  return arg;
}
function firstElement<T>(arr: T[]): T | undefined {
  return arr.length > 0 ? arr[0] : undefined;
}
function map<I, O>(arr: I[], f: (x: I) => O): O[] {
  return arr.map(f);
}
console.log(identity("s"), identity(5), firstElement([3, 4]), firstElement<string>([]));
console.log(map(["1", "2", "3"], n => parseInt(n)), map([1, 2], n => n > 1));
