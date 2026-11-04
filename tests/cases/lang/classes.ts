class Point {
  constructor(public x: number, public y: number) {}
  dist(o: Point): number { return Math.hypot(this.x - o.x, this.y - o.y); }
  toString(): string { return `(${this.x}, ${this.y})`; }
}
const p = new Point(0, 0), q = new Point(3, 4);
console.log(p.dist(q), q.toString(), q.x, q.y);
q.x = 10;
console.log(q.toString());

class Animal {
  name: string;
  legs = 4;
  static count = 0;
  constructor(name: string) { this.name = name; Animal.count++; }
  speak(): string { return `${this.name} makes a sound`; }
  describe(): string { return `${this.name} has ${this.legs} legs; ${this.speak()}`; }
}
class Dog extends Animal {
  constructor(name: string, private breed: string) { super(name); }
  speak(): string { return `${this.name} the ${this.breed} barks`; }
}
class Bird extends Animal {
  constructor(name: string) { super(name); this.legs = 2; }
  speak(): string { return super.speak() + " (tweet)"; }
}
const zoo: Animal[] = [new Animal("Generic"), new Dog("Rex", "beagle"), new Bird("Tweety")];
for (const a of zoo) console.log(a.describe());
console.log(Animal.count);
console.log(zoo[1] instanceof Dog, zoo[1] instanceof Animal, zoo[2] instanceof Dog);

class IntStack {
  private items: number[] = [];
  push(x: number): void { this.items.push(x); }
  pop(): number | undefined { return this.items.pop(); }
  size(): number { return this.items.length; }
}
const st = new IntStack();
st.push(1); st.push(2);
console.log(st.pop(), st.size(), st.pop(), st.pop());
interface Shape { area(): number; name: string; }
class Circle implements Shape {
  name = "circle";
  constructor(private r: number) {}
  area(): number { return Math.PI * this.r * this.r; }
}
class Sq implements Shape {
  name = "square";
  constructor(private s: number) {}
  area(): number { return this.s * this.s; }
}
const shapes: Shape[] = [new Circle(1), new Sq(3)];
for (const s of shapes) console.log(s.name, s.area().toFixed(3));
class Node2 {
  next: Node2 | null = null;
  constructor(public val: number) {}
}
let head: Node2 | null = null;
for (let i = 1; i <= 4; i++) { const n = new Node2(i); n.next = head; head = n; }
let sum = 0;
let cur = head;
while (cur !== null) { sum += cur.val; cur = cur.next; }
console.log("list sum", sum);
class Counter {
  private n = 0;
  inc = (): number => ++this.n;
}
const cnt = new Counter();
cnt.inc(); cnt.inc();
console.log(cnt.inc());
