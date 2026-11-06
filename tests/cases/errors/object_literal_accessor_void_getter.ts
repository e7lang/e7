// @error: the getter of 'x' must return a value
const o = {
  get x() {
    console.log("x");
  },
};
console.log(o);
