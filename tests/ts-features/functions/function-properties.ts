// @feature: Properties on functions and `call` / `apply` / `bind`
// @category: More on Functions
// @status: unsupported
// @why: verifiable bytecode: functions are closures with a fixed shape, not objects
// @instead: a class or an object literal holding the function and the data; call functions directly
// @error: function values have no properties
function greet(name: string): string { return "hi " + name; }
console.log(greet.call(null, "ann"), greet.length);
