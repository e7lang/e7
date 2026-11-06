interface Cfg { name: string; tags: string[]; limits: Map<string, number>; when: Date; opt?: number | undefined }
const shared = ["x"];
const cfg: Cfg = { name: "a", tags: shared, limits: new Map([["k", 1]]), when: new Date(0) };
const c = structuredClone(cfg);
c.tags.push("y");
c.limits.set("k", 2);
console.log(cfg, c, c.when.getTime(), cfg.tags === c.tags);
const pair = structuredClone([shared, shared]);
console.log(pair[0] === pair[1], pair[0] === shared, structuredClone(5), structuredClone("s"), structuredClone(new Uint8Array([1, 2])));
const s = structuredClone(new Set([1, 2]));
console.log(s, structuredClone({ a: undefined as number | undefined }));
