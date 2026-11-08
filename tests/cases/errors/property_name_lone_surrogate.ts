// @error: a property name with an unpaired surrogate
const o = { "\uD800": 1 };
console.log(o);
