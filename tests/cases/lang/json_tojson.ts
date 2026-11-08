// JSON.stringify calls toJSON() / toJSON(key)
class Money { constructor(public cents: number) {} toJSON(): string { return (this.cents / 100).toFixed(2); } }
class K { toJSON(key: string): string { return "k=" + key; } }
class N { toJSON(): number { return 7; } }
const o = { price: new Money(1999), list: [new K(), new K()], k: new K(), n: new N(), plain: 1 };
console.log(JSON.stringify(o));
console.log(JSON.stringify(o, null, 2));
console.log(JSON.stringify(new K()), JSON.stringify([new Money(5)]));
const lit = { a: 1, toJSON(): string { return "lit"; } };
console.log(JSON.stringify({ lit }));
