// @feature: `as const` assertions
// @category: Everyday Types
// @status: unsupported
// @why: not implemented yet: readonly literal types are not represented
// @instead: annotate the type, such as `const t: readonly [number, string] = [1, "x"]`
// @error: 'as const' is not supported
const req = { url: "https://example.com", method: "GET" } as const;
console.log(req.method);
