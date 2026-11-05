// @feature: Property access, nested objects and arrays of objects
// @category: Object Types
// @status: supported
interface Item { sku: string; qty: number; tags: string[] }
interface Order { id: number; items: Item[]; customer: { name: string; vip?: boolean } }
const o: Order = {
  id: 7,
  items: [{ sku: "A", qty: 2, tags: ["x"] }, { sku: "B", qty: 1, tags: [] }],
  customer: { name: "Ann" },
};
o.items[1].qty += 4;
o.customer.vip = true;
console.log(o.items.map(i => i.qty), o.customer, o.items[0].tags[0]);
console.log(o);
