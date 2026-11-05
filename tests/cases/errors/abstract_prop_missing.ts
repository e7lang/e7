// @error: does not implement inherited abstract property 'kind'
abstract class Shape {
  abstract kind: string;
}
class Sq extends Shape {}
console.log(new Sq());
