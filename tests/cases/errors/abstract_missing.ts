// @error: non-abstract class 'Sq' does not implement inherited abstract member 'area' from class 'Shape'
abstract class Shape {
  abstract area(): number;
}
class Sq extends Shape {}
console.log(new Sq());
