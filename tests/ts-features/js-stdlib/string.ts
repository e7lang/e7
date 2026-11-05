// @feature: `String` methods
// @category: JavaScript: Standard Library
// @status: supported
// @note: `split` / `replace` take string patterns; `localeCompare` compares code units
const s = "Hello, World";
console.log(s.length, s.charAt(1), s.charCodeAt(0), s.codePointAt(0), s.at(-1), s.indexOf("o"), s.lastIndexOf("o"));
console.log(s.includes("World"), s.startsWith("Hell"), s.endsWith("!"), s.slice(-5), s.substring(7, 9), s.toUpperCase());
console.log("  x ".trim() + "|", "ab".padStart(5, "-"), "ab".padEnd(4, "*"), "na".repeat(3), "a,b,,c".split(","), "a-b-c".replaceAll("-", "+"));
console.log("x.y.z".replace(".", "/"), "abc".split(""), String.fromCharCode(72, 105), String.fromCodePoint(0x1f600), "abc".concat("d", "e"));
console.log("price: 10".replace("10", m => String(Number(m) * 2)));
