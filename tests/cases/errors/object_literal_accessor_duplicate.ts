// @error: an object literal cannot have multiple get/set accessors with the same name ('x')
const o = {
  get x(): number {
    return 1;
  },
  get x(): number {
    return 2;
  },
};
console.log(o.x);
