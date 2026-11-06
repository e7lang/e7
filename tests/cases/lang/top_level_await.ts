import { later } from "e7:test";
console.log("before");
const v = await later(5);
console.log("after", v);
for (const x of [1, 2]) {
  const y = await later(x * 2);
  console.log(y);
}
