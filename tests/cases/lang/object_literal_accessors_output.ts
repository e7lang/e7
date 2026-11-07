// The accessors of an object literal are own enumerable properties, in the literal's order:
// Object.keys lists them; JSON.stringify, spread, rest, Object.values / entries and Object.assign
// call the getters; console.log prints [Getter] / [Setter] / [Getter/Setter] without calling them (as
// Node.js). A class's accessors are not own properties, also through an interface type.

let reads = 0;
const temp = {
  celsius: 25,
  get fahrenheit(): number {
    reads++;
    return this.celsius * 1.8 + 32;
  },
  set fahrenheit(f: number) {
    this.celsius = (f - 32) / 1.8;
  },
  get kelvin() {
    reads++;
    return this.celsius + 273.15;
  },
  set note(s: string) {
    console.log("note", s);
  },
  unit: "C",
};
console.log(temp);
console.log(reads);
console.log(Object.keys(temp));
console.log(JSON.stringify(temp));
console.log(JSON.stringify(temp, null, 2));
console.log(reads);
console.log("%j", temp);
console.log("%o / %O", { a: 1, get b() { return 2; } }, { get c() { return 3; } });
console.log(String(temp), `${temp}`);

// spread, rest, Object.values / entries / assign: getter values (data properties of the copy)
const flags = {
  on: true,
  get off(): boolean {
    return !this.on;
  },
  get text() {
    return this.on ? "yes" : "no";
  },
};
const copy = { ...flags };
flags.on = false;
console.log(copy, flags, copy.off, flags.off);
const { on, ...rest } = flags;
console.log(on, rest);
console.log(Object.values({ a: 1, get b() { return 2; }, c: 3 }));
console.log(Object.entries({ get x() { return "x"; }, y: "y" }));
const target = { a: 0, b: 0 };
Object.assign(target, { a: 5, get b() { return 7; } });
console.log(target, Object.assign({}, flags));

// nested objects and arrays
const tree = {
  label: "root",
  get size(): number {
    return this.children.length;
  },
  children: [
    { label: "a", get size() { return 0; } },
    { label: "b", get size() { return 0; } },
  ],
  deep: { deeper: { deepest: { get x() { return 1; } } } },
};
console.log(tree);
console.log(JSON.stringify(tree));
const list = [tree.children[0], { label: "c", get size() { return 2; } }];
console.log(list, JSON.stringify(list));

// keys that are numbers or need quotes
const keyed = {
  z: 0,
  get 2() {
    return "two";
  },
  get "a b"() {
    return 1;
  },
  get 1() {
    return "one";
  },
};
console.log(keyed, Object.keys(keyed), JSON.stringify(keyed));

// a setter only: no value (JSON.stringify leaves it out)
const sink = {
  set item(s: string) {
    console.log("item", s);
  },
};
sink.item = "x";
console.log(sink, Object.keys(sink), JSON.stringify(sink));

// a getter returning an object with a getter; undefined and functions are left out by JSON
const outer = {
  get inner() {
    return { get v() { return 42; } };
  },
  get nothing(): string | undefined {
    return undefined;
  },
  get big(): number {
    return 1e21;
  },
};
console.log(outer.inner.v, JSON.stringify(outer), outer);

// through an interface: an object literal's accessors are own properties, a class's are not
interface Named {
  get name(): string;
}
class Person implements Named {
  first = "Ada";
  get name(): string {
    return this.first + "!";
  }
}
const named: Named[] = [new Person(), { get name() { return "lit"; } }];
console.log(named, JSON.stringify(named), named.map((n) => Object.keys(n)));
interface Pair {
  a: number;
  get b(): number;
}
class PairClass implements Pair {
  a = 1;
  get b(): number {
    return 2;
  }
}
const pairs: Pair[] = [new PairClass(), { a: 3, get b() { return 4; } }];
console.log(pairs.map((p) => Object.values(p)), pairs.map((p) => Object.entries(p)));

// a getter throwing during JSON.stringify
const bad = {
  ok: 1,
  get boom(): number {
    throw new Error("boom");
  },
};
try {
  console.log(JSON.stringify(bad));
} catch (e) {
  if (e instanceof Error) console.log("caught", e.message);
}
console.log(bad);
