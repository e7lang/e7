// @feature: Narrowing of property paths
// @category: Narrowing
// @status: supported
class Node1 {
  next: Node1 | null = null;
  constructor(public value: number) {}
}
class List {
  head: Node1 | null = null;
  push(v: number): void {
    const n = new Node1(v);
    n.next = this.head;
    this.head = n;
  }
  second(): number {
    if (this.head !== null && this.head.next !== null) return this.head.next.value;
    return -1;
  }
}
const l = new List();
l.push(1);
console.log(l.second());
l.push(2);
console.log(l.second());
