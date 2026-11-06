// @feature: Well-known symbols (`Symbol.iterator`, `Symbol.toPrimitive`, ...)
// @category: Symbols
// @status: unsupported
// @why: not implemented yet: `Symbol` values (only `[Symbol.iterator]` is accepted, as a generator method)
// @instead: ordinary methods (`toString()`, `toArray()`)
// @error: 'Symbol' is not supported in E7
class Temperature {
  constructor(public c: number) {}
  [Symbol.toPrimitive](hint: string): number | string { return hint === "string" ? `${this.c}C` : this.c; }
}
const t = new Temperature(21);
console.log(+t, `${t}`);
