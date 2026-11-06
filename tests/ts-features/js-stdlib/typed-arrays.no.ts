// @feature: Typed arrays, `ArrayBuffer`, `DataView`
// @category: JavaScript: Standard Library
// @status: partial
// @why: not implemented yet: `SharedArrayBuffer` and `Atomics` (no threads in a script)
// @instead: an `ArrayBuffer`; the host shares data through calls
// @error: unknown type 'SharedArrayBuffer'
const sab: SharedArrayBuffer = new SharedArrayBuffer(8);
console.log(sab.byteLength);
