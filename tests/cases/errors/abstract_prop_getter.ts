// @error: abstract property 'kind' must be implemented as a field
abstract class Shape {
  abstract kind: string;
}
class Sq extends Shape {
  get kind(): string {
    return "sq";
  }
}
console.log(new Sq().kind);
