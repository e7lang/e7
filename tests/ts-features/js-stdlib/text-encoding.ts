// @feature: `TextEncoder` / `TextDecoder`
// @category: JavaScript: Standard Library
// @status: supported
const bytes = new TextEncoder().encode("héllo €");
console.log(bytes, bytes.length, new TextDecoder().decode(bytes), new TextDecoder().decode(new Uint8Array([0xff, 0x41])));
