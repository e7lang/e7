// readonly T[], ReadonlyArray<T>, readonly tuples, ReadonlyMap / ReadonlySet and readonly properties:
// reading works as for the mutable types; a mutable value is accepted where a readonly one is expected
interface Config { readonly name: string; readonly ports: readonly number[] }
function total(xs: readonly number[]): number { return xs.reduce((a, b) => a + b, 0); }
const ports: number[] = [80, 443];
const cfg: Config = { name: "web", ports };
ports.push(8080);  // the same array: the readonly view sees it
const pair: readonly [string, number] = ["a", 1];
const names: ReadonlyArray<string> = ["x", "y"];
const lookup: ReadonlyMap<string, number> = new Map<string, number>([["a", 1]]);
const seen: ReadonlySet<string> = new Set<string>(["x"]);
const sorted = names.slice().sort();  // slice / map / filter / concat return mutable arrays
sorted.push("z");
console.log(cfg.name, total(cfg.ports), pair[0], pair[1], names.indexOf("y"), lookup.get("a"), seen.has("x"), sorted);
console.log(names.map((s) => s.toUpperCase()), names.filter((s) => s !== "x"), names.concat(["w"]), [...names, "v"]);
for (const [k, v] of lookup) console.log(k, v);
