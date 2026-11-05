// @feature: Destructuring in `for...of`
// @category: JavaScript: Syntax
// @status: supported
const pairs: [string, number][] = [["a", 1], ["b", 2]];
for (const [k, v] of pairs) console.log(k, v);
const people = [{ name: "Ann", age: 30 }, { name: "Bob", age: 25 }];
for (const { name, age = 0 } of people) console.log(name, age);
