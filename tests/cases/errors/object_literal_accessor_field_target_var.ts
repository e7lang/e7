// @error: property 'area' is an accessor in '{ get area(): number; }'; e7 reads the properties of 'HasArea' as fields, so declare it there as 'get area(): number'
interface HasArea {
  readonly area: number;
}
function show(s: HasArea): void {
  console.log(s.area);
}
const sq = {
  get area(): number {
    return 9;
  },
};
show(sq);
