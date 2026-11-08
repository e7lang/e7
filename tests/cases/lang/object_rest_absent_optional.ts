// an optional property the source does not have is not a property of the rest
interface P { id: number; meta?: number }
const p: P = { id: 1 };
const { id, ...rest } = p;
console.log(id, rest, Object.keys(rest), JSON.stringify(rest));
function g({ id, ...o }: P): void { console.log(id, o, Object.keys(o).length); }
g({ id: 2 });
g({ id: 3, meta: 4 });
for (const { id, ...o } of [p, { id: 5, meta: 6 }]) console.log(id, JSON.stringify(o));
