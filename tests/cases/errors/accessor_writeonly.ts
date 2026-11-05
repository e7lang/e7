// @error: property 'v' has no getter
class A {
  private x = 0;
  set v(n: number) {
    this.x = n;
  }
}
console.log(new A().v);
