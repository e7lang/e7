// @error: is not accessible outside class 'A' because it has a private identifier
class A {
  #secret = 1;
}
class B extends A {
  peek(): number {
    return this.#secret;
  }
}
console.log(new B().peek());
