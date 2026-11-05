// @error: property 'set' does not exist on type 'ReadonlyMap<string, number>'
const m: ReadonlyMap<string, number> = new Map<string, number>([["a", 1]]);
m.set("b", 2);
console.log(m.size);
