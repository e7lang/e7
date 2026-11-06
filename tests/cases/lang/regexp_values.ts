// RegExp values in the type system: parameters, fields, collections, optional values, match results
class Tokenizer {
  private rules: [string, RegExp][] = [
    ["num", /\d+(?:\.\d+)?/y],
    ["id", /[A-Za-z_]\w*/y],
    ["op", /[-+*\/=()]/y],
    ["ws", /\s+/y],
  ];
  tokens(src: string): string[] {
    const out: string[] = [];
    let pos = 0;
    while (pos < src.length) {
      let matched = false;
      for (const [kind, re] of this.rules) {
        re.lastIndex = pos;
        const m = re.exec(src);
        if (m) {
          if (kind !== "ws") out.push(`${kind}:${m[0]}`);
          pos = re.lastIndex;
          matched = true;
          break;
        }
      }
      if (!matched) throw new Error(`unexpected '${src[pos]}' at ${pos}`);
    }
    return out;
  }
}
console.log(new Tokenizer().tokens("x1 = 3.25 * (y + 42)"));
try {
  new Tokenizer().tokens("a # b");
} catch (e) {
  if (e instanceof Error) console.log(e.message);
}

function firstMatch(patterns: RegExp[], s: string): RegExpExecArray | null {
  for (const p of patterns) {
    const m = p.exec(s);
    if (m) return m;
  }
  return null;
}
const found = firstMatch([/zz/, /(o)(o)/, /f/], "food");
console.log(found, found?.index);
const byName = new Map<string, RegExp>([["digits", /^\d+$/], ["hex", /^0x[0-9a-f]+$/i]]);
for (const [name, re] of byName) console.log(name, re.test("0xFF"), re.test("123"));
function pick(n: number): RegExp | null {
  return n > 0 ? /x/ : null;
}
console.log(pick(0)?.test("x") ?? false, pick(1)?.test("x") ?? false, [/a/, /b/g].map((r) => r.global));

// a match result is a string[]
function lengths(parts: string[]): number[] {
  return parts.map((p) => p.length);
}
const kv = /(\w+)=(\w*)/.exec("key=value");
if (kv) {
  console.log(lengths(kv), kv.slice(1), kv.includes("value"), kv.length, kv.indexOf("key"));
  const copy: string[] = [...kv];
  copy.push("extra");
  console.log(copy, kv.map((x) => x.toUpperCase()));
}
const all: RegExpMatchArray | null = "a1b2".match(/\d/g);
console.log(all, all?.index, all?.input, all?.length);
const one: RegExpMatchArray | null = "a1b2".match(/(\d)/);
console.log(one?.index, one?.input, one?.[1]);

// lookarounds, backreferences, named references
console.log("price: $42, cost: €17".match(/(?<=\$)\d+/), "abcabc".match(/(?<x>abc)\k<x>/)?.groups);
console.log("aaa bbb ccc ab".match(/\b(\w)\1+\b/g), "2024-11-06".replace(/(?<y>\d+)-(?<m>\d+)-(?<d>\d+)/, "$<d>.$<m>.$<y>"));
console.log("camelCaseString".replace(/(?<!^)([A-Z])/g, "_$1").toLowerCase(), "  trim me  ".replace(/^\s+|\s+$/g, ""));

// indices (d flag)
const di = /(?<w>o)(x)?/d.exec("foo");
console.log(di?.indices, di?.indices?.[1]);

// structuredClone keeps the pattern (not lastIndex) and a match's index, input and groups
const sticky = /a/g;
sticky.lastIndex = 2;
const cloned = structuredClone({ re: sticky, m: /(?<v>b)/.exec("ab") });
console.log(cloned, cloned.re.lastIndex, cloned.re === sticky, cloned.m?.groups?.v);
