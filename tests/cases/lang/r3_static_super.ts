// third test round of 0.1.3 (static_super): must print what Node.js prints
class A { static hello(): string { return "A"; } static get v(): number { return 1; } }
class B extends A { static override hello(): string { return "B>" + super.hello(); } }
console.log(B.hello());
