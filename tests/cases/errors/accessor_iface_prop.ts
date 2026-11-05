// @error: e7 reads interface properties as fields, so declare it in the interface as 'get area(): number'
interface HasArea {
  readonly area: number;
}
class Sq implements HasArea {
  get area(): number {
    return 4;
  }
}
console.log(new Sq().area);
