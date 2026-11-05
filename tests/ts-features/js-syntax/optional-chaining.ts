// @feature: Optional chaining (`a?.b`, `a?.[i]`, `f?.()`)
// @category: JavaScript: Syntax
// @status: supported
interface User { name: string; address?: { city: string; zip?: string }; tags?: string[]; greet?: () => string }
const u: User = { name: "Ann", tags: ["x"] };
const v: User = { name: "Bob", address: { city: "Oslo" }, greet: () => "hi" };
console.log(u.address?.city, v.address?.city, v.address?.zip?.length, u.tags?.[0], v.tags?.[0]);
console.log(u.greet?.(), v.greet?.(), u.address?.city.length);
