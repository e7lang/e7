// @error: e7 needs distinct private names in a class hierarchy
class A {
  #x = 1;
}
class B extends A {
  #x = 2;
}
console.log(new B());
