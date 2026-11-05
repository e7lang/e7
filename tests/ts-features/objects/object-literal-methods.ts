// @feature: Methods in object literals
// @category: Object Types
// @status: supported
interface Counter { n: number; inc(by: number): number }
const counter: Counter = {
  n: 0,
  inc(by: number): number { this.n += by; return this.n; },
};
counter.inc(2);
console.log(counter.inc(3), counter.n);
