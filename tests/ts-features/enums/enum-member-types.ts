// @feature: Enum members as types (`E.A`)
// @category: Enums
// @status: unsupported
// @why: not implemented yet: literal types are not represented
// @instead: the enum type, with a run-time check
// @error: unknown type 'ShapeKind.Circle'
enum ShapeKind { Circle, Square }
interface Circle { kind: ShapeKind.Circle; radius: number }
const c: Circle = { kind: ShapeKind.Circle, radius: 100 };
console.log(c);
