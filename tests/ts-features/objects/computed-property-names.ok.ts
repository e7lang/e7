// @feature: Computed property names
// @category: Object Types
// @status: partial
// @note: keys known at compile time: string and number literals, consts of literal types, enum members (in object literals, classes and interfaces, and `obj[KEY]`)
const key = "width";
enum Side { Top = "top" }
const box = { [key]: 10, height: 5, [Side.Top]: 1 };
console.log(box, box[key]);
