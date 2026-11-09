// @error: type 'number' is not assignable to type '1 | 2'
// n++ of n: 1 | 2 is a number afterwards (as in tsc)
type N = 1 | 2;
let n: N = 1;
n++;
const z: N = n;
console.log(z);
