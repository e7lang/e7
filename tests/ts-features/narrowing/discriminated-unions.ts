// @feature: Discriminated unions
// @category: Narrowing
// @status: unsupported
// @why: types known at compile time: union types other than with `null` / `undefined` need run-time type tags
// @instead: one interface with a discriminating field (string or enum) and optional properties
// @error: union types other than 'T | null | undefined' are not supported
interface Circle { kind: "circle"; radius: number }
interface Square { kind: "square"; side: number }
type Shape = Circle | Square;
function area(s: Shape): number {
  switch (s.kind) {
    case "circle": return Math.PI * s.radius ** 2;
    case "square": return s.side ** 2;
  }
}
console.log(area({ kind: "square", side: 2 }));
