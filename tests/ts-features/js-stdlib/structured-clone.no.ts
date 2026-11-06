// @feature: `structuredClone`
// @category: JavaScript: Standard Library
// @status: partial
// @why: types known at compile time: a clone of a class instance is a plain object in JavaScript, which its static type would not describe
// @instead: a `clone()` method on the class
// @error: structuredClone cannot copy a class instance
class Point { constructor(public x: number) {} }
console.log(structuredClone(new Point(1)));
