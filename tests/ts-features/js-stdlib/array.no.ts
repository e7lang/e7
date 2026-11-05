// @feature: `Array` methods
// @category: JavaScript: Standard Library
// @status: partial
// @why: not implemented yet: the ES2023 copying methods (`toSorted`, `toReversed`, `toSpliced`, `with`)
// @instead: copy, then sort: `[...xs].sort()`
// @error: method 'toSorted' does not exist
const xs = [3, 1, 2];
console.log(xs.toSorted(), xs.toReversed(), xs.with(0, 9));
