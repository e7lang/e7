// @feature: Contextually typed anonymous functions
// @category: Everyday Types
// @status: supported
const names = ["Alice", "Bob", "Eve"];
names.forEach(s => console.log(s.toUpperCase()));
const lengths = names.map(function (s) { return s.length; });
console.log(lengths, names.filter(n => n.length === 3));
