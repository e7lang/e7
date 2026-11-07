// Getters and setters in object literals: `this` is the object (its fields, other accessors and
// methods); getter / setter pairs, getter only (read-only), setter only; compound assignment and
// ++ call the getter and the setter once each; closures captured by accessors; nested literals.

const temp = {
  celsius: 20,
  get fahrenheit(): number {
    return this.celsius * 1.8 + 32;
  },
  set fahrenheit(f: number) {
    this.celsius = (f - 32) / 1.8;
  },
  get kelvin() {
    return this.celsius + 273.15;
  },
  describe(): string {
    return `${this.celsius.toFixed(1)}C = ${this.fahrenheit.toFixed(1)}F = ${this.kelvin.toFixed(2)}K`;
  },
};
console.log(temp.fahrenheit, temp.kelvin, temp.describe());
temp.fahrenheit = 212;
console.log(temp.celsius, temp.describe());
temp.fahrenheit += 18;
console.log(temp.celsius.toFixed(3));

// the setter's parameter takes the getter's type; the getter's type is inferred
const counter = {
  n: 0,
  get value() {
    return this.n;
  },
  set value(v) {
    console.log("set", v);
    this.n = v;
  },
};
counter.value++;
++counter.value;
counter.value *= 10;
console.log(counter.value, counter.n);
// the getter's type from the setter's parameter type
const scaled = {
  raw: 7,
  get value() {
    return this.raw / 2;
  },
  set value(v: number) {
    this.raw = v * 2;
  },
};
scaled.value -= 0.5;
console.log(scaled.value, scaled.raw);

// a setter only: writes call it
const lines: string[] = [];
const sink = {
  prefix: "> ",
  set line(s: string) {
    lines.push(this.prefix + s);
  },
};
sink.line = "one";
sink.line = "two";
console.log(lines);

// closures captured by accessors, a fresh object per call
function makeRange(lo: number, hi: number) {
  let calls = 0;
  return {
    lo,
    hi,
    get size(): number {
      calls++;
      return this.hi - this.lo;
    },
    get calls() {
      return calls;
    },
    get mid(): number {
      return this.lo + this.size / 2;
    },
    widen(by: number): void {
      this.lo -= by;
      this.hi += by;
    },
  };
}
const r1 = makeRange(0, 10);
const r2 = makeRange(5, 6);
r1.widen(5);
console.log(r1.size, r1.mid, r1.calls, r2.size, r2.calls);

// accessors in nested literals: `this` is the innermost object
const config = {
  name: "app",
  server: {
    host: "localhost",
    port: 8080,
    get url(): string {
      return `http://${this.host}:${this.port}`;
    },
  },
  get title(): string {
    return this.name.toUpperCase() + " @ " + this.server.url;
  },
};
config.server.port = 9090;
console.log(config.server.url, config.title);

// boolean, integer, bigint, optional and array values
const misc = {
  n: 3,
  get even(): boolean {
    return this.n % 2 === 0;
  },
  get big(): bigint {
    return BigInt(this.n) * 10n;
  },
  get label(): string | undefined {
    return this.n > 2 ? "many" : undefined;
  },
  get pair(): number[] {
    return [this.n, this.n * 2];
  },
};
console.log(misc.even, misc.big, misc.label, misc.pair);
misc.n = 2;
console.log(misc.even, misc.big, misc.label ?? "few", misc.pair.length);

// keys that are strings and numbers, and computed constant keys
const KEY = "computed";
const keyed = {
  get "two words"() {
    return 2;
  },
  get 7() {
    return "seven";
  },
  get [KEY]() {
    return "c";
  },
};
console.log(keyed["two words"], keyed[7], keyed.computed, keyed[KEY]);

// a getter that throws
const fragile = {
  get value(): number {
    throw new RangeError("not ready");
  },
};
try {
  console.log(fragile.value);
} catch (e) {
  if (e instanceof RangeError) console.log("caught", e.message);
}

// destructuring calls the getters
const { fahrenheit, kelvin } = temp;
console.log(fahrenheit.toFixed(1), kelvin.toFixed(2));
