// @error: type 'readonly string[]' is not assignable to type 'string[]'
const ro: readonly string[] = ["a"];
const rw: string[] = ro;
console.log(rw);
