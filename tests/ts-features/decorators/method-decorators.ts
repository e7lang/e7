// @feature: Method decorators
// @category: Decorators
// @status: unsupported
// @why: verifiable bytecode: a decorator replaces a method with a run-time value
// @instead: call a wrapper explicitly inside the method, or use a higher-order function
// @error: expected property name
function logged(target: () => string, ctx: ClassMethodDecoratorContext): () => string {
  console.log("decorating " + String(ctx.name));
  return target;
}
class Greeter {
  @logged
  greet(): string { return "hi"; }
}
console.log(new Greeter().greet());
