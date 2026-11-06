const DIRS = ["up", "down"] as const;
const cfg = { mode: "fast", level: 3 } as const;
const t = [1, "a", true] as const;
function move(d: "up" | "down"): string { return "moving " + d; }
console.log(move(DIRS[0]), DIRS, cfg, cfg.mode, t, t.length);
