// @feature: Object spread
// @category: JavaScript: Syntax
// @status: supported
interface Defaults { food: string; price: number; ambiance: string }
const defaults: Defaults = { food: "spicy", price: 10, ambiance: "noisy" };
const search = { ...defaults, food: "rich" };
class Point { x = 1; y = 2; len(): number { return 0; } }
const pt = { ...new Point(), z: 3 };
console.log(search, pt);
