// @error: private name '#y' is not declared by class 'A'
class A {
  #x = 1;
  get(): number {
    return this.#y;
  }
}
console.log(new A().get());
