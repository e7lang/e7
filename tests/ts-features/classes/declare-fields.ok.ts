// @feature: `declare` fields
// @category: Classes
// @status: partial
// @note: a field declared for the type checker and set elsewhere (by the constructor), also redeclaring an inherited field with its type
class Base {
  declare id: string;
  count = 1;
  constructor() { this.id = "b1"; }
}
class Derived extends Base {
  declare count: number;
}
const d = new Derived();
console.log(d.id, d.count);
