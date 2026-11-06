// @error: groups of a pattern built at run time are not known at compile time
const m = new RegExp("(?<a>x)").exec("x");
console.log(m?.groups);
