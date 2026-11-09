// fourth test round of 0.1.3 (types/override_inferred_return): must print what Node.js prints
type K = "a" | "b";
class A { kind(): K { return "a"; } async get(): Promise<string | null> { return null; } }
class B extends A { kind() { return "b" as const; } async get() { return "x"; } }
async function main(): Promise<void> {
  const xs: A[] = [new A(), new B()];
  console.log(xs.map((x) => x.kind()), await xs[1].get());
}
main();
