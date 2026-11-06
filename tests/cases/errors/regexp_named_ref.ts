// @error: Invalid regular expression: /(?<n>a)\k<m>/u: Invalid named capture referenced
console.log(/(?<n>a)\k<m>/u.test("a"));
