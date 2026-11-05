// @feature: Methods as values (unbound methods)
// @category: Classes
// @status: unsupported
// @why: JavaScript-identical behaviour: an unbound method loses `this` in JavaScript; E7 rejects it rather than behave differently
// @instead: an arrow function `x => obj.method(x)`, or an arrow function field
// @error: cannot be used as a value; use an arrow function
class Formatter {
  prefix = "#";
  tag(s: string): string { return this.prefix + s; }
}
const f = new Formatter();
console.log(["a", "b"].map(f.tag));
