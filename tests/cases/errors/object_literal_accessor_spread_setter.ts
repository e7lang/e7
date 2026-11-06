// @error: spread is not supported for an object with the setter-only accessor 'w' (its value would be undefined); add a getter
const o = {
  set w(v: number) {
    console.log(v);
  },
};
console.log({ ...o });
