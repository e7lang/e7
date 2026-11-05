// @error: generic type 'Pair' requires 2 type argument(s)
interface Pair<A, B> {
  a: A;
  b: B;
}
const p: Pair<number> = { a: 1, b: 2 };
console.log(p);
