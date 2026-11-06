// @feature: Custom iterables (`[Symbol.iterator]()`)
// @category: Iterators and Generators
// @status: partial
// @note: a class whose `[Symbol.iterator]()` method returns a generator works with `for...of`, spread, `Array.from` and destructuring
class Countdown {
  constructor(public from: number) {}
  *[Symbol.iterator](): Generator<number> {
    for (let n = this.from; n > 0; n--) yield n;
  }
}
for (const n of new Countdown(3)) console.log(n);
console.log([...new Countdown(2)]);
