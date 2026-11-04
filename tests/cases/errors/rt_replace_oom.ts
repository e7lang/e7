// @runtime-error: out of memory
console.log("a".repeat(40000).replaceAll("a", "$`").length);
