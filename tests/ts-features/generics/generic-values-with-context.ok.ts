// @feature: Generic functions used as values
// @category: Generics
// @status: partial
// @note: when a contextual function type determines the type arguments
function identity<T>(x: T): T { return x; }
const xs: number[] = [1, 2].map(identity);
const f: (s: string) => string = identity;
console.log(xs, f("ok"));
