type Shape = "circle" | "square" | "triangle";
function assertNever(x: never): never { throw new Error("unexpected " + x); }
function sides(s: Shape): number {
  switch (s) {
    case "circle": return 0;
    case "square": return 4;
    case "triangle": return 3;
    default: return assertNever(s);
  }
}
function label(s: Shape): string {
  if (s === "circle") return "round";
  const t: "square" | "triangle" = s;
  return t === "square" ? "four" : "three";
}
enum Color { Red, Green }
function hex(c: Color): string {
  switch (c) {
    case Color.Red: return "#f00";
    case Color.Green: return "#0f0";
    default: { const no: never = c; return no; }
  }
}
function grouped(s: Shape): string {
  switch (s) {
    case "circle":
    case "square": {
      const u: "circle" | "square" = s;
      return "simple " + u;
    }
    default: {
      const v: "triangle" = s;
      return v;
    }
  }
}
console.log(sides("square"), label("circle"), label("triangle"), hex(Color.Green), grouped("square"), grouped("triangle"));
