// @error: object literals cannot implement the accessor 'size'
interface Sized {
  get size(): number;
}
const s: Sized = { size: 3 };
console.log(s.size);
