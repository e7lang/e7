// A variable narrowed to a non-nullable type before a loop stays narrowed in and after the loop when every
// assignment in the loop stores a non-nullable value (an operator result, a literal, a string method of itself).
function h(m: Map<string, string>): string {
  let v = m.get("k");
  if (v === undefined) v = "fb";
  for (let i = 0; i < 2; i++) v = v + "!";
  return v;
}
function g(m: Map<string, string>): string {
  let v = m.get("k");
  if (v === undefined) v = "fb";
  for (let i = 0; i < 2; i++) v = v.toUpperCase();
  return v;
}
function k(m: Map<string, string>): string {
  let v = m.get("k");
  if (v === undefined) v = "fb";
  while (v.length < 5) { v = v + "x"; }
  return v;
}
function n(xs: number[]): number {
  let t: number | null = null;
  t = 0;
  for (const x of xs) { t += x; if (x > 2) t = t * 2; }
  return t;
}
function d(s: string | null): string {
  let r = s;
  if (r === null) r = "";
  do { r = r.padStart(r.length + 1, "-"); } while (r.length < 4);
  return r;
}
console.log(h(new Map()), g(new Map([["k", "ab"]])), k(new Map()), n([1, 2, 3]), d(null), d("abcdef"));
