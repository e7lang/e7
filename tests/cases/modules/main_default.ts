// default exports and imports: classes, functions (anonymous and generic), expressions, interfaces, names,
// live `export { x as default }` and copied `export default <let>`
import Vec, { Shape } from "./_lib/geometry.ts";
import obj, { hitCount } from "./_lib/defaults.ts";
import lastOr, * as fn from "./_lib/default_fn.ts";
import level, { raise, snapshot } from "./_lib/default_live.ts";
import { default as copied, count, bumpCount } from "./_lib/default_snapshot.ts";
import type Labelled from "./_lib/default_iface.ts";
import parity from "./_lib/default_name.ts";
import * as G from "./_lib/geometry.ts";

const v = new Vec(3, 4).plus(Vec.zero());
console.log(v, v.length(), Vec.created, v instanceof G.default, new Shape("s").name);
console.log(obj, obj.weight, hitCount());
console.log(lastOr([1, 2, 3], 0), lastOr<string>([], "none"), fn.tag, fn.default(["x"], "y"));
console.log(level, snapshot);
raise();
raise();
console.log(level, snapshot);
console.log(copied, count, bumpCount(), copied, count);
const l: Labelled = { label: "L" };
console.log(l, parity(3), parity(10));
class Vec3 extends Vec {
  constructor(x: number, y: number, public z: number) {
    super(x, y);
  }
  length(): number {
    return Math.hypot(this.x, this.y, this.z);
  }
}
const vs: Vec[] = [new Vec3(2, 3, 6), new G.default(1, 0)];
console.log(vs.map((x) => x.length()), Vec.created, G.default.created);
