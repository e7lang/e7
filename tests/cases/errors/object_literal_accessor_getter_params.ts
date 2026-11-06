// @error: a 'get' accessor cannot have parameters
const o = {
  get x(n: number): number {
    return n;
  },
};
console.log(o);
