// @error: property value cannot be void
// T inferred as void: E7 has no void values to store
interface R<T> { value: T | null }
function run<T>(f: () => T): R<T> { return { value: f() }; }
const r = run((): void => { console.log("x"); });
console.log(r.value === null);
