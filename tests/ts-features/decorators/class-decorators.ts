// @feature: Class decorators
// @category: Decorators
// @status: unsupported
// @why: verifiable bytecode: a decorator replaces or changes a class at run time, but E7 class layouts are fixed at compile time
// @instead: a factory or registration function called explicitly after the class
// @error: decorators are not supported
const registry: string[] = [];
function register(target: unknown, ctx: ClassDecoratorContext): void {
  registry.push(String(ctx.name));
}
@register
class Service {}
console.log(registry, new Service());
