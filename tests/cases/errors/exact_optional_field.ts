// @error: not assignable to the optional property 'x?: number'
class P { x?: number; }
const p = new P();
p.x = undefined;
console.log(p);
