// @runtime-error: stack overflow
function inf(n: number): number { return inf(n + 1) + 1; }
inf(0);
