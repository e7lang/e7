// @error: type '5' is not assignable to type 'E'
enum E { A = 1, B = 2 }
const x: E = 5;
console.log(x);
