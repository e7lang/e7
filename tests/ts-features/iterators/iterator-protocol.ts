// @feature: Custom iterables (`[Symbol.iterator]()`)
// @category: Iterators and Generators
// @status: unsupported
// @why: not implemented yet: `for...of` and spread take arrays, strings, `Map`, `Set` and `Uint8Array`
// @instead: a method that returns an array (`toArray()`), or a `forEach`-style method
// @error: computed members / index signatures are not supported
class Countdown {
  constructor(public from: number) {}
  [Symbol.iterator](): Iterator<number> {
    let n = this.from;
    return { next: (): IteratorResult<number> => n > 0 ? { value: n--, done: false } : { value: 0, done: true } };
  }
}
for (const n of new Countdown(3)) console.log(n);
