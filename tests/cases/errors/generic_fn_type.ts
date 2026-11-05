// @error: generic function types (<T>(x: T) => T) are not supported
type Id = <T>(x: T) => T;
const f: Id | null = null;
console.log(f);
