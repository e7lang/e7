// re-exports: `export { x } from`, `export { x as y } from`, `export * from`, `export * as ns from`
import Vec, { fromA, fromB, renamedA, aDefault, clash, shared, ticks, tick, geo, clamp, Level } from "./_lib/reexport_hub.ts";
import type { Label, Sized } from "./_lib/reexport_hub.ts";
import * as hub from "./_lib/reexport_hub.ts";
import { B, TAU, geo as geo2 } from "./_lib/reexport_outer.ts";

console.log(fromA, fromB, renamedA, aDefault(), clash, shared());
console.log(ticks, tick(), ticks, hub.ticks, B.tick(), ticks, B.ticks);
const v = new Vec(6, 8);
const w = new geo.default(1, 1);
const b: geo.Box<number> = new geo.Box(2);
const s: Sized = { size: 3 };
const lab: Label = { text: "t" };
console.log(v.length(), w instanceof hub.default, b.map((n) => n * 21).value, s, lab);
console.log(clamp(12, 0, 10), Level.High, hub.Level.Low, TAU.toFixed(3), geo2.Unit.Cm, hub.geo.move());
console.log(hub.clash, hub.fromB, hub.aDefault(), new hub.geo.Shape("hex").name);
