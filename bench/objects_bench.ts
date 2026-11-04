class Vec { constructor(public x: number, public y: number, public z: number) {} 
  add(o: Vec): Vec { return new Vec(this.x + o.x, this.y + o.y, this.z + o.z); }
  dot(o: Vec): number { return this.x * o.x + this.y * o.y + this.z * o.z; } }
const t0 = Date.now();
let acc = new Vec(0, 0, 0);
let d = 0;
for (let i = 0; i < 2000000; i++) { const v = new Vec(i, i * 0.5, 1); acc = acc.add(v); d += v.dot(acc) * 1e-12; }
console.log(Math.round(acc.x), Math.round(d), "objects", Date.now() - t0, "ms");
