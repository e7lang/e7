// typed array lengths are converted as JavaScript does (2.5 is 2, NaN is 0); RangeError messages; parseInt
// in radixes that are not powers of two
const n = 2.5;
console.log(new Uint8Array(n).length, new ArrayBuffer(n).byteLength, new Float32Array(NaN).length, new Int16Array(3.9).length);
try { (255).toString(1); } catch (e) { if (e instanceof Error) console.log(e.message); }
try { String.fromCodePoint(0x110000); } catch (e) { if (e instanceof Error) console.log(e.message); }
try { "a".repeat(-1); } catch (e) { if (e instanceof Error) console.log(e.message); }
try { new Uint8Array(-1); } catch (e) { if (e instanceof Error) console.log(e.name, e.message); }
// (values above 2^53 in these radixes: Node.js gives different last digits on arm64 and x64, see SPEC section 9)
console.log(parseInt("zzzzzzzzzzzzzz", 36), parseInt("66666666666666666666666", 7), parseInt("zz", 36), parseInt("-1a", 11));
