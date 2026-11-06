// @feature: Enum members as types (`E.A`)
// @category: Enums
// @status: supported
// @note: an enum member as a type (`kind: ShapeKind.Circle`); e7 checks it as the enum's type (any member), tsc checks the member
enum ShapeKind { Circle, Square }
interface Circle { kind: ShapeKind.Circle; radius: number }
const c: Circle = { kind: ShapeKind.Circle, radius: 100 };
console.log(c);
