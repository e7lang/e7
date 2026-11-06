// @feature: Getters and setters in object literals
// @category: Object Types
// @status: supported
// @note: needs e7vm 0.3.1; the accessors are part of the literal's type (`get x(): T`), so it is assignable to interfaces that declare them, not to ones that declare `x` as a property (read as a field); a getter and its setter have one type
interface Temperature {
  celsius: number;
  get fahrenheit(): number;
  set fahrenheit(v: number);
}
const temp: Temperature = {
  celsius: 20,
  get fahrenheit() { return this.celsius * 1.8 + 32; },
  set fahrenheit(f) { this.celsius = (f - 32) / 1.8; },
};
temp.fahrenheit = 212;
const point = { x: 3, y: 4, get length(): number { return Math.hypot(this.x, this.y); } };
console.log(temp.celsius, point.length, point, JSON.stringify(point), Object.keys(point));
