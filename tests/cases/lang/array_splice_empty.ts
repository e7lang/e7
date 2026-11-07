// splice on an empty array (no storage yet) and at the end of an array: nothing to move
const a: number[] = [];
console.log(a.splice(0, 0), a.splice(0, 1), a);
a.splice(0, 0, 1, 2);
console.log(a, a.splice(2, 0), a.splice(1), a);
const b: string[] = [];
b.splice(0, 5, "x");
console.log(b);
