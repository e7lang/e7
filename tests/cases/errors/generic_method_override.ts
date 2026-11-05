// @error: cannot override or be overridden
class A {
  conv<T>(x: T): T[] {
    return [x];
  }
}
class B extends A {
  conv<T>(x: T): T[] {
    return [x, x];
  }
}
console.log(new B().conv(1));
