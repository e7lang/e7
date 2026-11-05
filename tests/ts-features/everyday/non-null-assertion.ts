// @feature: Non-null assertion operator `x!`
// @category: Everyday Types
// @status: supported
// @note: checked at run time (a failed assertion ends the call)
const scores = new Map<string, number>([["ann", 7]]);
const s = scores.get("ann")! + 1;
const list: number[] | undefined = [1, 2];
console.log(s, list!.length);
