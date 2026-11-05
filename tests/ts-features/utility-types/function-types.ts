// @feature: `Parameters`, `ReturnType`, `ConstructorParameters`, `InstanceType`
// @category: Utility Types
// @status: unsupported
// @why: types known at compile time: they need `typeof` types and conditional types
// @instead: name the types with aliases and use them in the declarations
// @error: 'typeof' types are not supported
function f(a: number, b: string): boolean { return a > b.length; }
class C { constructor(public x: number) {} }
type P = Parameters<typeof f>;
type R = ReturnType<typeof f>;
type CP = ConstructorParameters<typeof C>;
type I = InstanceType<typeof C>;
const p: P = [1, "a"];
const r: R = f(...p);
const cp: CP = [3];
const i: I = new C(...cp);
console.log(r, i.x);
