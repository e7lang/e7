// private accessors (get #x / set #x, also static): direct calls, hidden from Object.keys and console.log
class Temperature {
  #celsius = 20;
  get #kelvin(): number { return this.#celsius + 273.15; }
  set #kelvin(k: number) { this.#celsius = k - 273.15; }
  static #count = 0;
  static get #next(): number { return ++Temperature.#count; }
  id: number;
  constructor() { this.id = Temperature.#next; }
  report(): string { return `${this.#kelvin.toFixed(2)} K #${this.id}`; }
  warm(by: number): void { this.#kelvin += by; this.#kelvin = this.#kelvin * 1; }
  same(o: Temperature): boolean { return this.#kelvin === o.#kelvin; }
}
class Sub extends Temperature { show(): string { return this.report(); } }
const t = new Temperature();
t.warm(5);
const s = new Sub();
console.log(t.report(), s.show(), t.same(s), Object.keys(t), t);
