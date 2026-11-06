// @feature: Custom iterables (`[Symbol.iterator]()`)
// @category: Iterators and Generators
// @status: partial
// @why: not implemented yet: an iterator written by hand (an object with a `next()` method); iterators are generators at run time
// @instead: a generator: `*[Symbol.iterator]() { ... yield v; }`
// @error: an object literal is not assignable to type 'Iterator<number>'
class Countdown {
  constructor(public from: number) {}
  [Symbol.iterator](): Iterator<number> {
    let n = this.from;
    return { next: (): IteratorResult<number> => n > 0 ? { value: n--, done: false } : { value: 0, done: true } };
  }
}
for (const n of new Countdown(3)) console.log(n);
