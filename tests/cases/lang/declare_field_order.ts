// declare fields come after the initialized ones (assigned in the constructor)
class A {
  declare id: string;
  count = 1;
  constructor() { this.id = "b1"; }
}
console.log(new A(), JSON.stringify(new A()), Object.keys(new A()));
