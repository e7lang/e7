// typed array lengths are converted as JavaScript does (2.5 is 2, NaN is 0); RangeError messages; parseInt
// of large values in radixes that are not powers of two
const n = 2.5;
console.log(new Uint8Array(n).length, new ArrayBuffer(n).byteLength, new Float32Array(NaN).length, new Int16Array(3.9).length);
try { (255).toString(1); } catch (e) { if (e instanceof Error) console.log(e.message); }
try { String.fromCodePoint(0x110000); } catch (e) { if (e instanceof Error) console.log(e.message); }
try { "a".repeat(-1); } catch (e) { if (e instanceof Error) console.log(e.message); }
try { new Uint8Array(-1); } catch (e) { if (e instanceof Error) console.log(e.name, e.message); }
console.log(parseInt("9007199254740993", 36), parseInt("zzzzzzzzzzzzzz", 36), parseInt("66666666666666666666666", 7));
console.log(parseInt("250764172146303082558682243748748", 9), parseInt("757j08f0iee81c82925gi53", 20), parseInt("0000hei5b7lifl8e91309", 23));
