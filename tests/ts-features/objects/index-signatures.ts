// @feature: Index signatures `{ [key: string]: T }`
// @category: Object Types
// @status: unsupported
// @why: types known at compile time: properties are fixed fields with static types
// @instead: `Map<string, T>` (`JSON.parse(text) as Map<string, T>` reads a JSON object with arbitrary keys)
// @error: index signatures are not supported
interface StringArray { [index: number]: string }
const prices: { [sku: string]: number } = { pen: 2 };
prices["book"] = 12;
const arr: StringArray = ["a"];
console.log(prices, arr[0]);
