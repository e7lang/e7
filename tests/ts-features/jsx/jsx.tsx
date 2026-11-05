// @feature: JSX (`.tsx` files)
// @category: JSX
// @status: unsupported
// @why: not implemented yet: JSX needs a UI runtime that E7 does not have
// @instead: build strings or data structures with functions
// @error: expected '>' but found 'id'
const el = <p id="greeting">Hello</p>;
console.log(el);
declare global {
  namespace JSX {
    type Element = string;
    interface IntrinsicElements { p: { id?: string } }
  }
}
