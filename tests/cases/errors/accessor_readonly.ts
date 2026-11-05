// @error: cannot assign to 'area' because it is a read-only property
class Sq {
  side = 2;
  get area(): number {
    return this.side ** 2;
  }
}
const s = new Sq();
s.area = 5;
