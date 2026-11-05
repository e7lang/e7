// @feature: Literal types
// @category: Everyday Types
// @status: partial
// @why: not implemented yet: numeric and boolean literal types are not represented
// @instead: `number` / `boolean` (or an enum) with a run-time check
// @error: numeric literal types are not supported
type Dice = 1 | 2 | 3 | 4 | 5 | 6;
const roll: Dice = 4;
console.log(roll);
