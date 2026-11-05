// @feature: `in` operator narrowing
// @category: Narrowing
// @status: unsupported
// @why: types known at compile time: object layouts are fixed by the static type, so the presence of a property is known
// @instead: a discriminating field, optional properties compared with `undefined`, or `instanceof`
// @error: the 'in' operator is not supported
interface Fish { swim: () => string }
interface Bird { fly: () => string }
function move(animal: Fish | Bird): string {
  if ("swim" in animal) return animal.swim();
  return animal.fly();
}
console.log(move({ swim: () => "swimming" }));
