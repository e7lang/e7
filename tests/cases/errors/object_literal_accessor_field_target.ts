// @error: property 'area' is an accessor in the object literal; e7 reads the properties of 'HasArea' as fields, so declare it there as 'get area(): number'
interface HasArea {
  readonly area: number;
}
const side = 3;
const sq: HasArea = {
  get area() {
    return side * side;
  },
};
console.log(sq.area);
