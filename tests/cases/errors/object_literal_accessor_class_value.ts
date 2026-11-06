// @error: the type of an object literal with accessors only holds such object literals
class C {
  get x(): number {
    return 1;
  }
}
let o = {
  get x(): number {
    return 2;
  },
};
o = new C();
console.log(o.x);
