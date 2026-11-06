// @feature: `as const` assertions
// @category: Everyday Types
// @status: supported
// @note: on literals and on arrays and objects of literals (readonly tuples and objects of literal types)
const req = { url: "https://example.com", method: "GET" } as const;
const dirs = ["up", "down"] as const;
function go(d: "up" | "down"): string { return "go " + d; }
console.log(req.method, go(dirs[1]), dirs.length);
