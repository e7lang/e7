// @feature: Typed arrays, `ArrayBuffer`, `DataView`
// @category: JavaScript: Standard Library
// @status: partial
// @note: `Uint8Array`, `Int8Array` ... `Float64Array`, `BigInt64Array`, `BigUint64Array` (elements little-endian), `ArrayBuffer` and `DataView` (all getters and setters, both byte orders)
const buf = new ArrayBuffer(8);
const bytes = new Uint8Array(buf);
const view = new DataView(buf);
view.setUint32(0, 0xdeadbeef);
view.setInt16(4, -2, true);
console.log(bytes, view.getUint32(0).toString(16), view.getInt16(4, true), bytes.subarray(1, 3), Uint8Array.from([1, 2, 300]));
view.setFloat64(0, Math.PI);
console.log(view.getFloat64(0), bytes.slice(0, 2), bytes.length, buf.byteLength, bytes.indexOf(0), bytes.join("-"));
