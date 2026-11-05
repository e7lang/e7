// @feature: ECMAScript `#private` members
// @category: Classes
// @status: partial
// @note: private fields and methods (instance and static); not listed by `Object.keys`, `JSON.stringify` or `console.log`
class Counter {
  #count = 0;
  static #instances = 0;
  constructor() { Counter.#instances++; }
  #bump(by: number): void { this.#count += by; }
  inc(): number { this.#bump(1); return this.#count; }
  static instances(): number { return Counter.#instances; }
}
const c = new Counter();
c.inc();
new Counter();
console.log(c.inc(), Counter.instances(), JSON.stringify(c), Object.keys(c), c);
