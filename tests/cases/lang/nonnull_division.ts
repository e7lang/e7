// A '/' after a non-null assertion is a division.
interface B {
  v?: number;
  a?: number[];
}
const b: B = { v: 3912, a: [10] };
const x = b.v! / 1000;
const y = b.a![0]! / 2;
const z = (b.v)! / 4 / 2;
console.log(x, y, z, !b.v ? 1 : 0);
