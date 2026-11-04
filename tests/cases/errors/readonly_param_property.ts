// @error: because it is a read-only property
class P { constructor(readonly w: number) {} }
const p = new P(1);
p.w += 1;
