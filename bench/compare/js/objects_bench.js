const log = typeof print === "function" ? print : console.log;
class Vec { constructor(x, y, z) { this.x = x; this.y = y; this.z = z; }
  add(o) { return new Vec(this.x + o.x, this.y + o.y, this.z + o.z); }
  dot(o) { return this.x * o.x + this.y * o.y + this.z * o.z; } }
const t0 = Date.now();
let acc = new Vec(0, 0, 0);
let d = 0;
for (let i = 0; i < 2000000; i++) { const v = new Vec(i, i * 0.5, 1); acc = acc.add(v); d += v.dot(acc) * 1e-12; }
log(Math.round(acc.x), Math.round(d), "objects", Date.now() - t0, "ms");
