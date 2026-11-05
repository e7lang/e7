// @feature: Implementing an interface property with an accessor
// @category: Classes
// @status: unsupported
// @why: types known at compile time: interface properties are read as fields, so the class must have a field
// @instead: declare the accessor in the interface: `interface HasArea { get area(): number }`
// @error: property 'area' is an accessor in the class
interface HasArea { readonly area: number }
class Square implements HasArea {
  constructor(public side: number) {}
  get area(): number { return this.side ** 2; }
}
const s: HasArea = new Square(3);
console.log(s.area);
