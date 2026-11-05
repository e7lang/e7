// @feature: `var` declarations
// @category: Variable Declaration
// @status: unsupported
// @why: not implemented yet: function-scoped, hoisted variables (`let` and `const` cover their uses)
// @instead: `let` or `const`
// @error: 'var' is not supported
var count = 0;
for (var i = 0; i < 3; i++) count += i;
console.log(count, i);
