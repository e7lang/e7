// @feature: Optional methods in interfaces (`m?(): T`)
// @category: Object Types
// @status: supported
// @note: an optional method is an optional property of function type: call it with `obj.m?.()`, or after checking it
interface Greeter {
  name: string;
  greet?(): string;
}
const g: Greeter = { name: "Ann" };
const h: Greeter = { name: "Bob", greet: () => "hi" };
console.log(g.greet?.() ?? "no greeting", h.greet ? h.greet() : "-");
