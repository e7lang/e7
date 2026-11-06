// @feature: `declare` fields
// @category: Classes
// @status: partial
// @why: types known at compile time: redeclaring an inherited field with a narrower type (`number` of a `number | string` field) would change its representation
// @instead: keep the inherited field's type, or a separate field
// @error: union types other than 'T | null | undefined' are not supported
class Base { value: number | string = 1; }
class Derived extends Base {
  declare value: number;
}
console.log(new Derived().value);
