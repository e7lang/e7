// @feature: Computed and constant-expression enum members
// @category: Enums
// @status: partial
// @why: not implemented yet: enum members are compile-time constants in E7; a computed member would be evaluated at run time
// @instead: a constant expression, or a `const` variable next to the enum
// @error: enum member initializers must be constant expressions
enum E { G = "123".length }
console.log(E.G);
