// @feature: Getters and setters (instance and static)
// @category: Classes
// @status: supported
class Thermometer {
  private c = 0;
  static #count = 0;
  get fahrenheit(): number { return this.c * 1.8 + 32; }
  set fahrenheit(f: number) { this.c = (f - 32) / 1.8; }
  static get count(): number { return Thermometer.#count; }
  static set count(v: number) { Thermometer.#count = v; }
}
const t = new Thermometer();
t.fahrenheit = 212;
t.fahrenheit += 18;
Thermometer.count++;
console.log(t.fahrenheit, Thermometer.count, Object.keys(t));
