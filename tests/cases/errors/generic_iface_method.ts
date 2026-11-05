// @error: generic methods in interfaces and object types are not supported
interface Mapper {
  map<U>(f: (x: number) => U): U[];
}
const m: Mapper | null = null;
console.log(m);
