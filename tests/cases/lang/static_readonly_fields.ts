// Static fields are writable (=, compound, ++); readonly fields only in their own constructor.
class Counter {
  static count = 0;
  static readonly LIMIT: number = 3;
  readonly id: number;
  constructor(public label: string, readonly weight: number) {
    this.id = ++Counter.count;
    this.weight = weight * 2;
  }
}
Counter.count = 10;
Counter.count += 5;
Counter.count++;
const c = new Counter("a", 2);
console.log(Counter.count, Counter.LIMIT, c.id, c.label, c.weight);
c.label = "b";
console.log(c.label);
