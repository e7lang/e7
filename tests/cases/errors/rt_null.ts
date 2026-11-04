// @runtime-error: map key not found
const m = new Map<string, number[]>();
m.get("x")!.push(1);
