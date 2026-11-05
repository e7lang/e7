// @error: '#x in obj' is not supported
class A {
  #x = 1;
  static has(o: A): boolean {
    return #x in o;
  }
}
console.log(A.has(new A()));
