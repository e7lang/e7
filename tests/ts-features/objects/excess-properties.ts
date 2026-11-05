// @feature: Assigning objects with more properties than the target type
// @category: Object Types
// @status: supported
// @note: an object with extra properties can be assigned to a narrower type (fresh literals with unknown properties are errors, as in tsc)
interface Named { name: string }
const full = { name: "box", width: 3 };
const n: Named = full;
console.log(n.name, JSON.stringify(n));
