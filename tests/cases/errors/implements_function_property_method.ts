// @error: e7 reads it as a property
interface H { handle: (x: number) => string }
class M implements H { handle(x: number): string { return "M" + x; } }
console.log(new M().handle(1));
