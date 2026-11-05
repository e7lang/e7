// @feature: Extending built-in classes (`Error`, `Map`, `Array`)
// @category: Classes
// @status: unsupported
// @why: not implemented yet: built-in types are not classes in E7
// @instead: throw `new Error(...)` (or `TypeError`, `RangeError`, ...) with a descriptive message; wrap a `Map` in a class field
// @error: a class can only extend another class
class ValidationError extends Error {
  constructor(public field: string) {
    super("invalid " + field);
    this.name = "ValidationError";
  }
}
try {
  throw new ValidationError("email");
} catch (e) {
  if (e instanceof ValidationError) console.log(e.field, e.message);
}
