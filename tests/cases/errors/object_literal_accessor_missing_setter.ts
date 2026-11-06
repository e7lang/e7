// @error: the object literal must implement the setter of 'size' declared by 'Sized'
interface Sized {
  get size(): number;
  set size(v: number);
}
const s: Sized = {
  get size() {
    return 1;
  },
};
console.log(s.size);
