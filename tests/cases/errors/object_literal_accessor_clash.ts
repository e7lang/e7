// @error: an object literal cannot have a property and an accessor with the same name ('x')
const o = {
  x: 1,
  get x(): number {
    return 2;
  },
};
console.log(o.x);
