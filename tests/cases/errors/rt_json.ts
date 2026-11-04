// @runtime-error: expected a number at $.a
interface T { a: number }
const t = JSON.parse('{"a": "x"}') as T;
