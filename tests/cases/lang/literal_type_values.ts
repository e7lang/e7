// literal types: constants passed where a literal type is expected, `as` assertions, inference from the
// expected type, boolean switches, enums used as their values
type Dir = "n" | "s";
const dm = new Map<Dir, number>();
dm.set("n", 1);
const ds = new Set<Dir>();
ds.add("s");
const dirs: Dir[] = ["n"];
dirs.push("s");
console.log(dm.get("n"), dm.has("s"), ds.has("s"), dirs.includes("n"), dirs.indexOf("s"));
const m2 = new Map<string, Dir | undefined>();
m2.set("a", "n");
console.log(m2.get("a"));

const a = "n" as Dir;
const input: string = "c";
const unchecked = input as Dir;  // not checked, as in TypeScript
let maybe: 1 | 2 | 3 | undefined = 2;
console.log(a, unchecked, (maybe as number) / 2);
const ro: readonly number[] = [1, 2];
const rw = ro as number[];
rw.push(3);
console.log(ro);

function id<T>(x: T): T { return x; }
function wrap<T>(x: T): { v: T } { return { v: x }; }
const d: Dir = id("n");
const w: { v: Dir } = wrap("s");
console.log(d, w);

function bsw(b: boolean): string { switch (b) { case true: return "T"; case false: return "F"; } }
console.log(bsw(true), bsw(false));
type Small = 1 | 2 | 4;
const smalls: Small[] = [1, 2, 4];
const one: 1 | 2 = 1;
console.log(Math.max(...smalls), ~one, -one);

enum Str { P = "p", Q = "q" }
enum Auto { X, Y, Z }
const sv: Str = Str.Q;
console.log(sv.length, sv.toUpperCase(), sv.padStart(3, "-"), ~Auto.Y);
const idx = [10, 20, 30];
console.log(idx[Auto.Z]);
let cnt = 0;
for (let i = Auto.X; i <= Auto.Z; i++) cnt += i;
console.log(cnt);
