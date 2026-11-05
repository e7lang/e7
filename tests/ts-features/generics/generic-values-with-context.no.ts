// @feature: Generic functions used as values
// @category: Generics
// @status: partial
// @why: types known at compile time: each instantiation is a separate function, so the type arguments must be known where the value is taken
// @instead: a contextual type (`const f: (x: number) => number = identity`) or a wrapper arrow
// @error: needs a contextual function type to be used as a value
function identity<T>(x: T): T { return x; }
const f = identity;
console.log(f(1));
