// @feature: Optional methods in interfaces (`m?(): T`)
// @category: Object Types
// @status: unsupported
// @why: not implemented yet: a method signature is always required (the `?` is ignored)
// @instead: an optional property with a function type: `m?: () => T`, called with `obj.m?.()`
// @error: is missing in object literal of type 'Greeter'
interface Greeter {
  name: string;
  greet?(): string;
}
const g: Greeter = { name: "Ann" };
console.log(g.greet?.() ?? "no greeting");
