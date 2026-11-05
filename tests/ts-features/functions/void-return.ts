// @feature: `void` return type and contextual `void`
// @category: More on Functions
// @status: supported
// @note: a `void`-returning function type accepts functions that return a value
const src = [1, 2, 3];
const dst: number[] = [];
src.forEach(el => dst.push(el * 10));
type VoidFunc = () => void;
const f1: VoidFunc = () => true;
f1();
console.log(dst);
