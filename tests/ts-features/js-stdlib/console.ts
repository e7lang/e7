// @feature: `console.log` / `info` / `warn` / `error` and format specifiers
// @category: JavaScript: Standard Library
// @status: supported
// @note: values are printed like Node.js `util.inspect`
console.log("%s is %d years (%i), %f, %j %o", "Ann", 42.5, 42.5, 1.5, { a: 1 }, [1]);
interface Rec { nested: { deep: { deeper: { deepest: number } } }; list: string[][]; n: number | null; u?: number }
const rec: Rec = { nested: { deep: { deeper: { deepest: 1 } } }, list: [["one"], ["two", "three"]], n: null };
console.info(rec);
console.log(new Map<number, { a: number[] }>([[1, { a: [1, 2] }]]), new Set(["x"]), [1.5, -0, NaN], "str", ["str"], 10n, [true, false]);
console.log(Array.from({ length: 30 }, (_, i) => i * 100));
