// @feature: Arrow function fields capturing `this`
// @category: Classes
// @status: supported
class Clicker {
  name = "clicker";
  count = 0;
  onClick = (): string => {
    this.count++;
    return `${this.name} ${this.count}`;
  };
}
const c = new Clicker();
const handler = c.onClick;
handler();
console.log(handler(), [1, 2].map(() => c.onClick()));
