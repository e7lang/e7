// @error: type 'number' is not assignable to type 'string'
console.log("abc".replace("b", (m: string) => m.length));
