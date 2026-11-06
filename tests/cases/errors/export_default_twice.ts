// @error: a module cannot have more than one default export
export default 1;
export default function f(): number {
  return 2;
}
