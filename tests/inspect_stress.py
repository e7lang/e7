#!/usr/bin/env python3
"""Differential stress test for console.log formatting (util.inspect): generates random E7 programs that build
random nested values (numbers, strings, bigints, arrays, tuples, records, class instances, Map, Set, Date,
Uint8Array, ArrayBuffer, cycles) and print them with console.log (single values, several arguments and printf-style
format strings), runs them with e7 and with Node.js and compares the output value by value.

Usage: tests/inspect_stress.py [--e7 build/e7] [--node node] [--programs 50] [--values 80] [--seed 1] [--keep DIR]
       tests/inspect_stress.py --seed N --values M --emit FILE.ts   (write one program, e.g. as a test case)
Exit code 1 when an output differs (the first differences are printed with the statement that produced them).
"""
import argparse
import difflib
import pathlib
import random
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent
SHIM = (ROOT / "node_shim" / "register.mjs").resolve().as_uri()

KEYS = ["a", "b", "x", "y", "name", "value", "_p", "$d", "a b", "x-y", "it's", "q\"", "1", "10", "2", "0",
        "01", "4294967295", "\u043a\u043b\u044e\u0447", "\u4e2d", "e\u0301", "longPropertyName", "length",
        "__proto2", "k\n"]
CHARS = (["a", "b", "z", "Q", "0", "9", " ", ".", ",", "-", "_"] * 6 +
         ["'", "\"", "`", "${", "\\", "\n", "\t", "\r", "\b", "\f", "\v", "\0", "\x01", "\x1b", "\x1f", "\x7f",
          "\x85", "\x9f", "\xa0", "\xe9", "\xdf", "\u4e2d", "\u6587", "\U0001F600", "\U0001F44D\U0001F3FD",
          "\u0301", "\u200b", "\ud800", "\udc00", "\uff21", "\u3000", "\u2028", "\ufeff", "%", "%s", "\u00ad",
          "\U0001F1EF\U0001F1F5"])


def ts_string(s):
    out = []
    for ch in s:
        c = ord(ch)
        if ch == "\\": out.append("\\\\")
        elif ch == "\"": out.append("\\\"")
        elif ch == "\n": out.append("\\n")
        elif ch == "\r": out.append("\\r")
        elif ch == "\t": out.append("\\t")
        elif c < 0x20 or 0x7f <= c <= 0x9f or 0xd800 <= c <= 0xdfff or c in (0x2028, 0x2029, 0xfeff, 0xad):
            out.append("\\u%04x" % c)
        elif c > 0xffff:
            out.append(ch)
        else: out.append(ch)
    return "\"" + "".join(out) + "\""


def ts_key(key):
    plain = key and (key[0].isascii() and (key[0].isalpha() or key[0] in "_$")) and all(
        c.isascii() and (c.isalnum() or c in "_$") for c in key)
    return key if plain else ts_string(key)


class Gen:
    def __init__(self, rnd):
        self.r = rnd
        self.classes = []   # (name, fields [(name, type)], parent index or None)
        self.prelude = []
        self.ncls = 0

    # ---------------------------------------------------------------- types
    def scalar_type(self):
        return self.r.choice(["number"] * 5 + ["string"] * 4 + ["boolean", "bigint", "i32", "u8", "Date"])

    def gen_type(self, d):
        r = self.r
        if d <= 0 or r.random() < 0.3:
            t = self.scalar_type()
            if r.random() < 0.12:
                return ("nullable", t, r.choice(["null", "undefined"]))
            return t
        k = r.choice(["array"] * 5 + ["tuple", "record", "record", "class", "map", "set", "bytes", "buffer",
                      "nullarr"])
        if k == "array":
            return ("array", self.gen_type(d - 1))
        if k == "nullarr":
            return ("array", ("nullable", self.scalar_type(), r.choice(["null", "undefined"])))
        if k == "tuple":
            return ("tuple", [self.gen_type(d - 1) for _ in range(r.randint(1, 4))])
        if k == "record":
            n = r.choice([0, 1, 2, 3, 4, 5, 6, 8, 12, 25]) if r.random() < 0.3 else r.randint(1, 5)
            keys = r.sample(KEYS, min(n, len(KEYS)))
            return ("record", [(key, self.gen_type(d - 1), r.random() < 0.15) for key in keys])
        if k == "class":
            return ("class", self.make_class(d))
        if k == "map":
            key = r.choice(["string", "number", "bigint", "boolean"]) if r.random() < 0.8 else self.key_type(d - 1)
            return ("map", key, self.gen_type(d - 1))
        if k == "set":
            return ("set", r.choice(["string", "number", "bigint"]) if r.random() < 0.7 else self.key_type(d - 1))
        if k == "bytes":
            return "Uint8Array"
        return "ArrayBuffer"

    def key_type(self, d):
        # Map keys / Set elements: not nullable (an e7 rule), not Date (e7 compares Dates by value there)
        while True:
            t = self.gen_type(d)
            if t != "Date" and (isinstance(t, str) or t[0] != "nullable"):
                return t

    def make_class(self, d):
        r = self.r
        if self.classes and r.random() < 0.4:
            return r.randrange(len(self.classes))
        parent = r.randrange(len(self.classes)) if self.classes and r.random() < 0.3 else None
        n = r.choice([0, 1, 2, 3, 5])
        fields = []
        used = set()
        p = parent
        while p is not None:
            used.update(f for f, _ in self.classes[p][1])
            p = self.classes[p][2]
        for i in range(n):
            nm = "f%d_%d" % (self.ncls, i)
            fields.append((nm, self.gen_type(d - 1)))
        name = r.choice(["Point", "Node", "Item", "Box", "Thing", "C"]) + str(self.ncls)
        self.ncls += 1
        self.classes.append((name, fields, parent))
        idx = len(self.classes) - 1
        params = [("p%d" % i, self.ts_type(t)) for i, (f, t) in enumerate(self.all_fields(idx))]
        own = len(fields)
        inherited = len(params) - own
        lines = ["class %s%s {" % (name, " extends " + self.classes[parent][0] if parent is not None else "")]
        for f, t in fields:
            lines.append("  %s: %s;" % (f, self.ts_type(t)))
        lines.append("  constructor(%s) {" % ", ".join("%s: %s" % p for p in params))
        if parent is not None:
            lines.append("    super(%s);" % ", ".join(p for p, _ in params[:inherited]))
        for i, (f, _) in enumerate(fields):
            lines.append("    this.%s = p%d;" % (f, inherited + i))
        lines.append("  }")
        lines.append("}")
        self.prelude.append("\n".join(lines))
        return idx

    def deep_type(self, t, seen=None):
        """the TypeScript type text including the field types of the classes it uses"""
        seen = set() if seen is None else seen
        text = self.ts_type(t)
        if isinstance(t, str):
            return text
        parts = [text]
        if t[0] == "class" and t[1] not in seen:
            seen.add(t[1])
            parts += [self.deep_type(ft, seen) for _, ft in self.all_fields(t[1])]
        elif t[0] in ("nullable", "array"):
            parts.append(self.deep_type(t[1], seen))
        elif t[0] == "tuple":
            parts += [self.deep_type(x, seen) for x in t[1]]
        elif t[0] == "record":
            parts += [self.deep_type(ft, seen) for _, ft, _ in t[1]]
        elif t[0] == "map":
            parts += [self.deep_type(t[1], seen), self.deep_type(t[2], seen)]
        elif t[0] == "set":
            parts.append(self.deep_type(t[1], seen))
        return " ".join(parts)

    def all_fields(self, idx):
        name, fields, parent = self.classes[idx]
        return (self.all_fields(parent) if parent is not None else []) + fields

    def ts_type(self, t):
        if isinstance(t, str):
            return t
        k = t[0]
        if k == "nullable":
            return "%s | %s" % (self.ts_type(t[1]), t[2])
        if k == "array":
            inner = self.ts_type(t[1])
            return ("(%s)[]" % inner) if "|" in inner or "=>" in inner else inner + "[]"
        if k == "tuple":
            return "[%s]" % ", ".join(self.ts_type(x) for x in t[1])
        if k == "record":
            return "{ %s }" % "; ".join("%s%s: %s" % (ts_key(key),
                                                      "?" if opt else "", self.ts_type(ft))
                                        for key, ft, opt in t[1])
        if k == "class":
            return self.classes[t[1]][0]
        if k == "map":
            return "Map<%s, %s>" % (self.ts_type(t[1]), self.ts_type(t[2]))
        if k == "set":
            return "Set<%s>" % self.ts_type(t[1])
        raise ValueError(t)

    # ---------------------------------------------------------------- values
    def num(self):
        r = self.r
        c = r.random()
        if c < 0.45: return str(r.randint(-20, 200))
        if c < 0.55: return str(r.randint(-10**9, 10**12))
        if c < 0.7: return repr(round(r.uniform(-1000, 1000), r.randint(0, 6)))
        return r.choice(["-0", "0", "NaN", "Infinity", "-Infinity", "1e21", "1e-7", "123456789012345680000",
                         "5e-324", "1.7976931348623157e308", "0.1 + 0.2", "2 ** 53", "-1e-7", "1 / 3", "100",
                         "1000000", "99999999999", "0.5", "-0.25"])

    def string(self):
        r = self.r
        c = r.random()
        if c < 0.03: return ts_string("ab'\"") + ".repeat(%d)" % r.randint(2500, 2600)
        if c < 0.2: n = r.randint(15, 120)
        else: n = r.choice([0, 1, 2, 3, 5, 8, 12])
        s = "".join(r.choice(CHARS) for _ in range(n))
        if r.random() < 0.15:
            s = "\n".join(s[i:i + 20] for i in range(0, len(s), 20)) + r.choice(["", "\n"])
        return ts_string(s)

    def scalar(self, t):
        r = self.r
        if t == "number": return self.num()
        if t == "string": return self.string()
        if t == "boolean": return r.choice(["true", "false"])
        if t == "bigint":
            return r.choice(["0n", "-1n", "%dn" % r.randint(-10**6, 10**6), "%dn" % r.randint(0, 10**30),
                             "-%dn" % r.randint(0, 10**25)])
        if t == "i32": return str(r.randint(-2**31, 2**31 - 1) if r.random() < 0.3 else r.randint(-50, 50))
        if t == "u8": return str(r.randint(0, 255))
        if t == "Date":
            return "new Date(%s)" % r.choice([str(r.randint(-10**13, 10**13)), "0", "NaN", "8.64e15", "-8.64e15",
                                              str(r.randint(0, 2 * 10**12))])
        raise ValueError(t)

    def length(self, d):
        r = self.r
        if r.random() < 0.08 and d <= 1: return r.choice([99, 100, 101, 102, 130])
        if r.random() < 0.3: return r.randint(6, 30)
        return r.choice([0, 1, 2, 3, 4, 5, 6, 7])

    def value(self, t, d=0):
        r = self.r
        if isinstance(t, str):
            if t == "Uint8Array":
                n = self.length(d)
                if r.random() < 0.3 or n == 0: return "new Uint8Array(%d)" % n
                return "new Uint8Array([%s])" % ", ".join(str(r.randint(0, 255) if r.random() < 0.5 else r.randint(0, 9)) for _ in range(n))
            if t == "ArrayBuffer":
                n = self.length(d)
                if n == 0: return "new ArrayBuffer(%d)" % r.choice([0, 3])
                return "new Uint8Array([%s]).buffer" % ", ".join(str(r.randint(0, 255)) for _ in range(n))
            return self.scalar(t)
        k = t[0]
        if k == "nullable":
            if r.random() < 0.4: return t[2]
            return self.value(t[1], d)
        if k == "array":
            n = self.length(d)
            if n > 12 and not self.small(t[1]): n = r.randint(0, 12)
            return "[%s]" % ", ".join(self.value(t[1], d + 1) for _ in range(n))
        if k == "tuple":
            return "[%s]" % ", ".join(self.value(x, d + 1) for x in t[1])
        if k == "record":
            parts = []
            for key, ft, opt in t[1]:
                if opt and r.random() < 0.5: continue
                v = self.value(ft, d + 1)
                if v == "undefined": v = "null" if "null" in self.ts_type(ft) else None
                if v is None: continue  # explicit undefined in a record field: e7 cannot tell it from a missing one
                parts.append("%s: %s" % (ts_key(key), v))
            if not parts and t[1] and all(opt for _, _, opt in t[1]):
                return "{}"
            # a non-optional field must be present: retry those with null-free values
            have = {p.split(":")[0] for p in parts}
            for key, ft, opt in t[1]:
                kk = ts_key(key)
                if not opt and kk not in have:
                    parts.append("%s: %s" % (kk, self.value(self.strip_undef(ft), d + 1)))
            r.shuffle(parts) if r.random() < 0.2 else None
            return "{ %s }" % ", ".join(parts)
        if k == "class":
            fs = self.all_fields(t[1])
            return "new %s(%s)" % (self.classes[t[1]][0], ", ".join(self.value(ft, d + 1) for _, ft in fs))
        if k == "map":
            n = self.length(d)
            if n > 12 and not self.small(t[2]): n = r.randint(0, 12)
            if n == 0: return "new Map<%s, %s>()" % (self.ts_type(t[1]), self.ts_type(t[2]))
            return "new Map<%s, %s>([%s])" % (self.ts_type(t[1]), self.ts_type(t[2]), ", ".join(
                "[%s, %s]" % (self.value(t[1], d + 1), self.value(t[2], d + 1)) for _ in range(n)))
        if k == "set":
            n = self.length(d)
            if n > 12 and not self.small(t[1]): n = r.randint(0, 12)
            if n == 0: return "new Set<%s>()" % self.ts_type(t[1])
            return "new Set<%s>([%s])" % (self.ts_type(t[1]), ", ".join(self.value(t[1], d + 1) for _ in range(n)))
        raise ValueError(t)

    def strip_undef(self, t):
        if not isinstance(t, str) and t[0] == "nullable" and t[2] == "undefined":
            return t[1]
        return t

    def small(self, t):
        return isinstance(t, str) or t[0] == "nullable"


def make_program(rnd, nvalues):
    g = Gen(rnd)
    body = []
    stmts = []
    for i in range(nvalues):
        c = rnd.random()
        if c < 0.08:  # cycles through class instances
            body.append("{")
            body.append("  const n1 = new GNode(%s); const n2 = new GNode(%s);" % (g.num(), g.num()))
            body.append("  n1.next = n2; n2.next = %s; n1.kids.push(%s);" % (rnd.choice(["n1", "n2", "null"]),
                                                                          rnd.choice(["n1", "n2", "new GNode(1)"])))
            body.append("  n2.kids.push(%s);" % rnd.choice(["n1", "n2", "n1, n2", "new GNode(7)"]))
            expr = rnd.choice(["n1", "n2", "[n1, n2]", "[n1, n1]", "{ a: n1, b: [n2] }", "new Map<string, GNode>([[\"k\", n1]])"])
            stmt = "console.log(%s);" % expr
            body.append("  console.log(\"#%d\");" % i)
            body.append("  " + stmt)
            body.append("}")
            stmts.append(stmt)
            continue
        t = g.gen_type(rnd.choice([1, 2, 3, 4, 5]))
        decl = "const v%d: %s = %s;" % (i, g.ts_type(t), g.value(t))
        if c < 0.75:
            stmt = "console.log(v%d);" % i
        elif c < 0.88:
            others = []
            for j in range(rnd.randint(1, 3)):
                t2 = g.gen_type(2) if rnd.random() < 0.5 else "string"
                body.append("const w%d_%d: %s = %s;" % (i, j, g.ts_type(t2), g.value(t2)))
                others.append("w%d_%d" % (i, j))
            stmt = "console.log(v%d, %s);" % (i, ", ".join(others))
        else:
            # %j: JSON.stringify throws on bigints, and e7's does not list integer keys first (a separate issue)
            deep = g.deep_type(t)
            json = "bigint" not in deep and not re.search(r'"\d+"', deep)
            fmt = "".join(rnd.choice(["%s", "%d", "%i", "%f", "%o", "%O", "%c", "%%", "%x", " ", "a", "%"] +
                                     ["%j"] * json) for _ in range(rnd.randint(1, 6)))
            extra = rnd.choice(["", ", 1", ", \"s\"", ", -0, true"] + [", -0, 2n"] * (not json))
            stmt = "console.log(%s, v%d%s);" % (ts_string(fmt), i, extra)
        body.append(decl)
        body.append("console.log(\"#%d\");" % i)
        body.append(stmt)
        stmts.append(decl + "\n" + stmt)
    prelude = ["class GNode { v: number; next: GNode | null = null; kids: GNode[] = []; constructor(v: number) { this.v = v; } }"]
    return "\n".join(prelude + g.prelude + body) + "\n", stmts


# features of the Node.js output, counted to show what the generated values exercised
FEATURES = {
    "multi-line": r"\n  ", "grouped columns": r"\n  \S+,  +\S", "right-aligned": r"\n +-?\d+, +-?\d",
    "... more items": r"more items?", "... more characters": r"more characters?", "... more bytes": r"more bytes?",
    "string split with +": r"['\"`] \+\n", "[Circular *n]": r"\[Circular \*", "<ref *2>": r"<ref \*2>",
    "[Array]": r"\[Array\]", "[Object]": r"\[Object\]", "[Map]/[Set]": r"\[(Map|Set)\]",
    "[Uint8Array]": r"\[Uint8Array\]", "[ClassName]": r"\[(Point|Node|Item|Box|Thing|C)\d+\]",
    "backtick quotes": r"`[^`\n]*'[^`\n]*`", "double quotes": r"\"[^\"\n]*'[^\"\n]*\"", "\\x escapes": r"\\x[0-9A-F]{2}",
    "lone surrogate \\u": r"\\ud[89a-f]", "wide characters": "[\u4e00-\u9fff\uff01-\uff60\U0001F300-\U0001FAFF]",
    "Invalid Date": r"Invalid Date", "extended year": r"[+-]\d{6}-", "empty items": r"(\[\]|\{\})",
    "quoted keys": r"'[^'\n]*': ", "ArrayBuffer": r"Uint8Contents", "Map": r"Map\(\d+\) \{", "Set": r"Set\(\d+\) \{",
    "%j JSON": r'\{"', "[length] (%o)": r"\[length\]",
}


def split(out):
    blocks, cur, key = {}, [], None
    for line in out.split("\n"):
        if line.startswith("#") and line[1:].isdigit():
            if key is not None: blocks[key] = "\n".join(cur)
            key, cur = int(line[1:]), []
        else:
            cur.append(line)
    if key is not None: blocks[key] = "\n".join(cur)
    return blocks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--e7", default=str(ROOT.parent / "build" / "e7"))
    ap.add_argument("--node", default="node")
    ap.add_argument("--programs", type=int, default=50)
    ap.add_argument("--values", type=int, default=80)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--keep", default=None, help="directory for the generated programs (default: temporary)")
    ap.add_argument("--max-report", type=int, default=5)
    ap.add_argument("--stmt-chars", type=int, default=600)
    ap.add_argument("--emit", default=None, help="only write program number 0 of the seed to this file")
    a = ap.parse_args()
    if a.emit:
        src, _ = make_program(random.Random(a.seed * 100003), a.values)
        header = ("// Generated by tests/inspect_stress.py --seed %d --values %d --emit: random values printed with\n"
                  "// console.log, compared with Node.js.\n" % (a.seed, a.values))
        pathlib.Path(a.emit).write_text(header + src, encoding="utf-8", errors="surrogatepass")
        return
    tmp = pathlib.Path(a.keep) if a.keep else pathlib.Path(tempfile.mkdtemp(prefix="e7_inspect_"))
    tmp.mkdir(parents=True, exist_ok=True)
    compared = failed = 0
    seen = dict.fromkeys(FEATURES, 0)
    for p in range(a.programs):
        rnd = random.Random(a.seed * 100003 + p)
        src, stmts = make_program(rnd, a.values)
        f = tmp / ("prog%d.ts" % p)
        f.write_text(src, encoding="utf-8", errors="surrogatepass")
        e = subprocess.run([a.e7, "--mem", "2000000000", str(f)], capture_output=True)
        n = subprocess.run([a.node, "--experimental-transform-types", "--no-warnings", "--import", SHIM, str(f)],
                           capture_output=True)
        eo, no = e.stdout.decode("utf-8", "surrogateescape"), n.stdout.decode("utf-8", "surrogateescape")
        if e.returncode != 0 or n.returncode != 0:
            print("%s: exit codes e7 %d node %d\n%s%s" % (f, e.returncode, n.returncode,
                                                           e.stderr.decode()[-2000:], n.stderr.decode()[-2000:]))
            failed += 1
            continue
        for name, rx in FEATURES.items():
            seen[name] += len(re.findall(rx, no))
        eb, nb = split(eo), split(no)
        for i in range(len(stmts)):
            compared += 1
            if eb.get(i) != nb.get(i):
                failed += 1
                if failed <= a.max_report:
                    diff = difflib.unified_diff((nb.get(i) or "").split("\n"), (eb.get(i) or "").split("\n"),
                                                "node", "e7", lineterm="", n=2)
                    print("--- %s value #%d differs\n%s\n%s\n" % (f, i, stmts[i][:a.stmt_chars],
                                                               "\n".join(list(diff)[:60])))
    print("features in the Node.js output: " + ", ".join("%s %d" % kv for kv in seen.items()))
    print("compared %d console.log calls in %d programs, %d differ (programs in %s)" % (compared, a.programs, failed, tmp))
    if not failed and not a.keep:
        shutil.rmtree(tmp, ignore_errors=True)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
