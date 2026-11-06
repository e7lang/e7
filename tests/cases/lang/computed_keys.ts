const KEY = "content-type";
enum Field { Name = "name", Age = "age" }
enum Num { One = 1 }
const headers = { [KEY]: "json", accept: "*/*" };
const person = { [Field.Name]: "ann", [Field.Age]: 3, [Num.One]: true };
interface Typed { [KEY]: string; [Field.Name]: string }
const t: Typed = { "content-type": "x", name: "y" };
class C {
  [KEY] = "v";
  ["literal"](): number { return 7; }
}
const c = new C();
console.log(headers, person, t, headers[KEY], c[KEY], c.literal(), Object.keys(c));
