// @error: the getter and setter of 'v' must have the same type ('number' and 'string')
const o = {
  n: 0,
  get v(): number {
    return this.n;
  },
  set v(s: string) {
    this.n = s.length;
  },
};
console.log(o.v);
