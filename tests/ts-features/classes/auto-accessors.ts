// @feature: Auto-accessors (`accessor x = 1`)
// @category: Classes
// @status: unsupported
// @why: not implemented yet
// @instead: a field, or a private field with `get` / `set` accessors
// @error: expected ';' but found 'name'
class Person {
  accessor name = "Ann";
}
console.log(new Person().name);
