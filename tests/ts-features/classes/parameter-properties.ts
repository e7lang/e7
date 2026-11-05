// @feature: Parameter properties (`constructor(public x: number)`)
// @category: Classes
// @status: supported
class Params {
  constructor(public readonly x: number, protected y: number, private z: number) {}
  sum(): number { return this.x + this.y + this.z; }
}
const a = new Params(1, 2, 3);
console.log(a.x, a.sum(), a);
