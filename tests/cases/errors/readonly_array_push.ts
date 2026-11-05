// @error: property 'push' does not exist on type 'readonly number[]'
function add(xs: readonly number[]): number { xs.push(1); return xs.length; }
console.log(add([1]));
