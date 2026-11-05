// @feature: `this` parameters
// @category: More on Functions
// @status: unsupported
// @why: not implemented yet: functions cannot be called with a chosen `this`
// @instead: pass the object as an ordinary parameter, or use a method
// @error: 'this' parameters are not supported
interface User { name: string }
function hello(this: User): string { return "hi " + this.name; }
const u = { name: "ann", hello };
console.log(u.hello());
