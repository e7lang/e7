class Animal { constructor(public name: string) {} }
class Dog extends Animal { bark(): string { return this.name + " woof"; } }
function isDog(a: Animal): a is Dog { return a instanceof Dog; }
function isDefined(s: string | undefined): s is string { return s !== undefined; }
function assertDefined(x: number | null, msg: string): asserts x is number {
  if (x === null) throw new Error(msg);
}
function check(cond: boolean): asserts cond {
  if (!cond) throw new Error("check failed");
}
type Kind = "a" | "b" | "c";
function isA(k: Kind): k is "a" { return k === "a"; }
const pets: Animal[] = [new Animal("cat"), new Dog("rex")];
for (const p of pets) {
  if (isDog(p)) console.log(p.bark());
  else console.log("not a dog", p.name);
}
const v: string | undefined = pets.length > 1 ? "x" : undefined;
if (isDefined(v)) console.log(v.length);
if (!isDefined(v)) console.log("undefined"); else console.log("defined", v.toUpperCase());
const n: number | null = pets.length;
assertDefined(n, "no n");
console.log(n + 1);
const s: string | null = pets.length > 0 ? "y" : null;
check(s !== null);
console.log(s.length);
const k: Kind = pets.length > 1 ? "b" : "a";
if (isA(k)) { const a: "a" = k; console.log(a); } else { const bc: "b" | "c" = k; console.log(bc); }
try { assertDefined(null, "boom"); } catch (e) { if (e instanceof Error) console.log(e.message); }
