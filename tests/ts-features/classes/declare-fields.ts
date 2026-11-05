// @feature: `declare` fields
// @category: Classes
// @status: unsupported
// @why: not implemented yet
// @instead: an ordinary field
// @error: 'declare' fields are not supported
class Base { value: number | string = 1; }
class Derived extends Base {
  declare value: number;
}
console.log(new Derived().value);
