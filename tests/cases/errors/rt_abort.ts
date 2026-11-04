// @runtime-error: custom failure
function check(x: number): void { if (x > 1) throw new Error("custom failure"); }
check(5);
