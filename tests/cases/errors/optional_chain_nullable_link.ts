// @error: object is possibly 'undefined'
// a link after '?.' still checks its own object: here `o` is optional (TypeScript: 'i.o' is possibly 'undefined')
interface I { o?: { x: number } }
const i: I | null = {};
console.log(i?.o.x);
