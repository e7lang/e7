// `export default count` of a `let`: the value at this point, not a live binding
export let count = 5;
export default count;
export function bumpCount(): number {
  return ++count;
}
