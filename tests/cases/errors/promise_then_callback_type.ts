// @error: does not accept the value of type 'A'
class A { a = 1; }
class B { b = 2; }
function readB(x: B): number { return x.b; }
Promise.resolve(new A()).then(readB);
