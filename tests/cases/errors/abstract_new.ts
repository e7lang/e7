// @error: cannot create an instance of an abstract class
abstract class Shape {
  abstract area(): number;
}
const s = new Shape();
console.log(s.area());
