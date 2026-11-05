// @feature: Labeled statements (`break` / `continue` with labels, labeled blocks)
// @category: JavaScript: Syntax
// @status: supported
outer: for (let i = 0; i < 3; i++) {
  for (let j = 0; j < 3; j++) {
    if (j === 1) continue outer;
    if (i === 2) break outer;
    console.log(i, j);
  }
}
block: {
  console.log("in block");
  if (Math.sqrt(4) === 2) break block;
  console.log("skipped");
}
