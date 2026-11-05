// @feature: Static fields and methods (inherited by subclasses)
// @category: Classes
// @status: supported
class MyClass {
  static x = 0;
  static printX(): string { return "x=" + MyClass.x; }
}
class Sub extends MyClass {}
MyClass.x += 2;
Sub.x = 7;
console.log(MyClass.printX(), Sub.x, MyClass.x, Sub.printX());
