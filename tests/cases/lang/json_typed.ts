interface Item { id: number; name: string; tags: string[]; price?: number; meta: { created: string; flags: boolean[] } | null }
interface Order { orderId: string; items: Item[]; pair: [string, number] }
const text = `{
  "orderId": "A-17",
  "items": [
    {"id": 1, "name": "pen", "tags": ["office", "blue"], "price": 1.5, "meta": {"created": "2024-01-01", "flags": [true, false]}},
    {"id": 2, "name": "pad", "tags": [], "meta": null}
  ],
  "pair": ["x", 9]
}`;
const o = JSON.parse(text) as Order;
console.log(o.orderId, o.items.length, o.items[0].tags.join("/"), o.items[0].price, o.items[1].price === undefined);
console.log(o.items[0].meta?.created, o.items[1].meta === null, o.items[0].meta!.flags.length);
console.log(o.pair[0], o.pair[1]);
console.log(JSON.stringify(o.items[0]));
const nums = JSON.parse("[[1,2],[3]]") as number[][];
console.log(nums[1][0], nums.length);
const s = JSON.parse('"\\u00e9\\n"') as string;
console.log(s.length, JSON.stringify(s));
const b = JSON.parse("true") as boolean;
console.log(b);
