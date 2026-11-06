// Error values outside try/catch: conversions match Node.js (try/catch security review)
const e = new TypeError("t");
const r = new RangeError("bad");
console.log(JSON.stringify(e), JSON.stringify({ err: r, n: 1 }), JSON.stringify([e]));
console.log(String([e, r]), [e, r].join(" | "));
const opt: Error | null = r;
console.log("" + opt, `${opt}`, `${e}`);
const none: Error | null = null;
console.log(`${none}`);
// holes of new Array<Error>(n) read as undefined: instanceof is false, as in JavaScript
const holes = new Array<Error>(2);
holes[1] = e;
console.log(holes[0] instanceof Error, holes[1] instanceof Error, holes[1] instanceof TypeError, holes[1] instanceof RangeError);
const errs: Error[] = [e, r, new Error("plain")];
console.log(errs.map((x: Error): string => x.name + "/" + x.message).join(","));
