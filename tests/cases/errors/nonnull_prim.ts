// @error: cannot be applied
const m = new Map<string, number>();
const v: number = m.get('a') + 1;
