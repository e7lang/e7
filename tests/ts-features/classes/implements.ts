// @feature: `implements` clauses
// @category: Classes
// @status: supported
// @note: checked structurally
interface Pingable { ping(): string }
interface Named { name: string }
class Sonar implements Pingable, Named {
  name = "sonar";
  ping(): string { return "ping from " + this.name; }
}
const p: Pingable = new Sonar();
console.log(p.ping());
