// @feature: Well-known symbols (`Symbol.iterator`, `Symbol.toPrimitive`, ...)
// @category: Symbols
// @status: unsupported
// @why: not implemented yet: property names are fixed identifiers
// @instead: ordinary methods (`toString()`, `toArray()`)
// @error: computed members / index signatures are not supported
class Temperature {
  constructor(public c: number) {}
  [Symbol.toPrimitive](hint: string): number | string { return hint === "string" ? `${this.c}C` : this.c; }
}
const t = new Temperature(21);
console.log(+t, `${t}`);
