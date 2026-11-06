// @error: function has more parameters than expected
// a pattern built at run time: the replacement function gets the match only
console.log("x".replace(new RegExp("(x)"), (m: string, p1: string) => p1));
