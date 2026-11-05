// @runtime-error: an e7 limit
// e7's own limits (here: JSON nesting) are not JavaScript errors, so try/catch does not catch them
let deep = "";
for (let i = 0; i < 300; i++) deep += "[";
for (let i = 0; i < 300; i++) deep += "]";
try {
  const v = JSON.parse(deep) as number[];
  console.log(v.length);
} catch (e) {
  console.log("caught");
}
