class Base {
  declare tag: string;
  count = 1;
  constructor() { this.tag = "t"; }
}
class Child extends Base {
  declare count: number;
  extra = 2;
}
const c = new Child();
console.log(new Base().tag, c.count, c.extra, c.tag);
