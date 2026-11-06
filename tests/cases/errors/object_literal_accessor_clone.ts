// @error: structuredClone cannot copy an object with getters or setters
const o = {
  n: 1,
  get double(): number {
    return this.n * 2;
  },
};
console.log(structuredClone(o));
