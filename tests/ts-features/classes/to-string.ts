// @feature: Overriding `toString()`
// @category: Classes
// @status: supported
// @note: called by `String(x)`, template literals and `+`
class Money {
  constructor(public cents: number) {}
  toString(): string { return `$${(this.cents / 100).toFixed(2)}`; }
}
const m = new Money(1234);
console.log(String(m), `${m}`, "total: " + m);
