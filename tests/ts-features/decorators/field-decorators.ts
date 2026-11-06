// @feature: Field and accessor decorators
// @category: Decorators
// @status: unsupported
// @why: verifiable bytecode: a decorator changes field initialization at run time
// @instead: initialize the field explicitly (or validate in a setter)
// @error: decorators are not supported
function double(_: undefined, ctx: ClassFieldDecoratorContext): (v: number) => number {
  return v => v * 2;
}
class Config {
  @double
  retries = 3;
}
console.log(new Config().retries);
