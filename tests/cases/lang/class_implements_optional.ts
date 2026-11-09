// an optional interface property that a class implementing the interface does not declare: undefined
// through the interface, not an own property until it is set
interface Animal { name: string; legs?: number; tags?: string[] }
class Fish implements Animal { name = "nemo"; }
class Shark extends Fish { teeth = 300; }
const zoo: Animal[] = [new Fish(), new Shark()];
for (const a of zoo) console.log(a.name, a.legs, a.tags?.length ?? 0, JSON.stringify(a), Object.keys(a));
console.log(zoo, { ...zoo[0] });
const f: Animal = new Fish();
f.legs = 0;
console.log(f, f.legs, Object.keys(f), JSON.stringify(f));
const sh = new Shark();
console.log(sh);
