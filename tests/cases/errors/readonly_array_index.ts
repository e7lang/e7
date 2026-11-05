// @error: index signature in type 'readonly number[]' only permits reading
const xs: ReadonlyArray<number> = [1, 2];
xs[0] = 3;
console.log(xs);
