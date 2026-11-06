// @error: type 'number[] | null' is not assignable to type 'number[]'
// only one path assigns a non-null value; the other may still be null
function ensure(list: number[] | null, flag: boolean): number[] {
  if (flag) list = [];
  return list;
}
console.log(ensure(null, true));
