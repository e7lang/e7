// Writing an array length truncates or extends it, exactly like JavaScript.
const a = [1, 2, 3, 4, 5];
a.length = 3;
console.log(a.length, a.join(","));
a.length = a.length - 1;
console.log(a.length, a.join(","));
const nested: number[][] = [[1, 2, 3], [4]];
nested[0].length = 1;
console.log(nested[0].length, nested[1].length);
