#!/usr/bin/env node
enum ShapeKind { Circle, Square }
interface Circle { kind: ShapeKind.Circle; radius: number }
interface Box { height: number }
interface Box { width: number }
type Shout = Uppercase<"hi there">;
type Cap = Capitalize<"word">;
type Name = NonNullable<string | null>;
type Res = Awaited<Promise<number>>;
class Builder {
  parts: string[] = [];
  add(p: string): this { this.parts.push(p); return this; }
}
class LoudBuilder extends Builder {
  shout(): string { return this.parts.join(" ").toUpperCase(); }
}
const c: Circle = { kind: ShapeKind.Circle, radius: 2 };
const b: Box = { height: 1, width: 2 };
const s: Shout = "HI THERE";
const w: Cap = "Word";
const n: Name = "x";
const r: Res = <number>(40 + 2);
console.log(c, b, s, w, n, r, new LoudBuilder().add("a").add("b").shout());
