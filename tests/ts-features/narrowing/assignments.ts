// @feature: Narrowing by assignment
// @category: Narrowing
// @status: supported
let x: string | null = null;
x = "hello";
console.log(x.length);
let y: number | undefined;
y = 3;
console.log(y * 2);
