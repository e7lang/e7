// static members are inherited: read through the prototype chain; a write through a subclass
// creates the subclass's own property
class A {
  static count: number = 0;
  static label: string = "a";
  static tag?: string;
  static readonly LIMIT: number = 10;
  static make(n: number): number {
    return n * 2;
  }
  static get twice(): number {
    return A.count * 2;
  }
  static set twice(v: number) {
    A.count = v / 2;
  }
  static fn: (x: number) => number = (x) => x + 1;
}
class B extends A {}
class C extends B {}
class D extends A {
  static count: number = 100;
}

console.log(B.count, C.count, B.label, C.LIMIT, B.make(3), C.make(4), B.twice, C.fn(1), B.tag);
A.count = 5;
console.log(A.count, B.count, C.count, D.count);
B.count++;
console.log(A.count, B.count, C.count);
A.count = 7;
console.log(A.count, B.count, C.count);
C.count += 10;
console.log(A.count, B.count, C.count);
B.count = 1;
console.log(A.count, B.count, C.count);
C.label += "!";
console.log(A.label, B.label, C.label);
B.tag ??= "t";
console.log(A.tag, B.tag, C.tag);
C.twice = 40;
console.log(A.count, B.count, C.count, C.twice);
D.count--;
console.log(A.count, D.count);
let k = C.count++;
console.log(k, C.count, ++C.count, B.count);

function bump(): void {
  B.label = B.label + "b";
}
bump();
bump();
console.log(A.label, B.label, C.label);

class Base<T> {
  static created: number = 0;
  value: T;
  constructor(v: T) {
    this.value = v;
    Base.created++;
  }
}
class NumBox extends Base<number> {}
new NumBox(1);
new Base<string>("s");
console.log(Base.created, NumBox.created);
NumBox.created = 50;
new NumBox(2);
console.log(Base.created, NumBox.created);

class G<T> extends A {
  item: T;
  constructor(item: T) {
    super();
    this.item = item;
  }
}
console.log(G.count, G.make(5), new G<string>("g").item);
G.count = 3;
console.log(A.count, G.count);
