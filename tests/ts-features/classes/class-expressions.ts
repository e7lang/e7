// @feature: Class expressions
// @category: Classes
// @status: unsupported
// @why: not implemented yet: classes are declared at the top level of a scope
// @instead: a class declaration
// @error: class expressions are not supported
const Point = class {
  x = 0;
  y = 0;
};
console.log(new Point());
