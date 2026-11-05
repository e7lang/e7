// @feature: Typed arrays, `ArrayBuffer`, `DataView`
// @category: JavaScript: Standard Library
// @status: partial
// @why: not implemented yet: typed arrays other than `Uint8Array` (`Int32Array`, `Float64Array`, ...), `SharedArrayBuffer`, `Atomics`
// @instead: `number[]`, fixed-width integer arrays such as `u32[]`, or a `DataView`
// @error: cannot find class 'Float64Array'
const f = new Float64Array(3);
f[0] = 1.5;
console.log(f);
