// @feature: Numeric enums (auto-increment and explicit values)
// @category: Enums
// @status: supported
enum Direction { Up = 1, Down, Left = 10, Right }
enum Flags { None = 0, Read = 1, Write = 2, Exec = 4 }
function move(d: Direction): string {
  switch (d) {
    case Direction.Up: return "up";
    case Direction.Down: return "down";
    default: return "sideways " + d;
  }
}
const rw = Flags.Read | Flags.Write;
console.log(Direction.Down, Direction.Right, move(Direction.Up), move(Direction.Right), rw, (rw & Flags.Exec) !== 0);
