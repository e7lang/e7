// @error: private accessors (get #x / set #x) are not supported
class A {
  #v = 1;
  get #x(): number {
    return this.#v;
  }
}
console.log(new A());
