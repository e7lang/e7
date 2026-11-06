// @feature: ECMAScript `#private` members
// @category: Classes
// @status: supported
// @note: private fields, methods and accessors (instance and static); not listed by `Object.keys`, `JSON.stringify` or `console.log`
class Counter {
  #count = 0;
  static #instances = 0;
  constructor() { Counter.#instances++; }
  #bump(by: number): void { this.#count += by; }
  get #doubled(): number { return this.#count * 2; }
  inc(): number { this.#bump(1); return this.#count; }
  doubled(): number { return this.#doubled; }
  static instances(): number { return Counter.#instances; }
}
const c = new Counter();
c.inc();
new Counter();
console.log(c.inc(), c.doubled(), Counter.instances(), JSON.stringify(c), Object.keys(c), c);
