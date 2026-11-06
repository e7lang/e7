// `export default <expression>`: evaluated once, where it stands, into a hidden constant
console.log("init defaults");
let hits = 0;
function weight(n: number): number {
  hits++;
  return n * 10;
}
export function hitCount(): number {
  return hits;
}
export default { name: "defaults", weight: weight(4) };
console.log("after export default", hits);
