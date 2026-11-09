// @error: object is possibly 'null'
// the clause of "b" is also reached from the clause of "a", where s became null
function f(s: string | null): number {
  switch (s) {
    case "a": s = null;
    case "b": return s.length;
  }
  return 0;
}
console.log(f("a"));
