// @error: method shorthand in an object literal with spread properties is not supported
const base = { n: 1 };
const o = {
  ...base,
  twice(): number {
    return 2;
  },
};
console.log(o.twice());
