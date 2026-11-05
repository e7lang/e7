// @feature: ECMAScript `#private` members
// @category: Classes
// @status: partial
// @why: not implemented yet: private accessors (`get #x()`)
// @instead: a private field or a private method
// @error: private accessors (get #x / set #x) are not supported
class Temperature {
  #celsius = 20;
  get #kelvin(): number { return this.#celsius + 273.15; }
  report(): string { return `${this.#kelvin} K`; }
}
console.log(new Temperature().report());
