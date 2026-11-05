// @error: a generic arrow function must be assigned to a 'const'
function f(): number {
  let id = <T,>(x: T): T => x;
  return id(1);
}
console.log(f());
