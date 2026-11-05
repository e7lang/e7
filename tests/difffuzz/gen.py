#!/usr/bin/env python3
"""Random E7 program generator for differential fuzzing (e7 vs Node.js).

Every generated program is meant to be:
  * valid strict TypeScript (`tsc --strict` with lib/e7.d.ts),
  * inside the E7 subset (docs/SPEC.md),
  * deterministic (no Math.random, no current date; dates are UTC),
  * free of the documented differences of SPEC section 9: no out-of-bounds reads (indexes go through
    `ix()` and a length check, or `at()` / `?? default`), no holes, fixed-width integer operations are
    always wrapped in their conversion function (`u32(a + b)`, same result on both, section 4.2) and
    never overflow 2^53 before the conversion, no localeCompare / non-Latin case conversion, and
    console.log only prints primitives (structures go through JSON.stringify).

Usage: gen.py [--seed S] [--index I] [-o out.ts]
The program for (seed, index) is always the same (random.Random seeded with a string).
"""
import argparse, random, re, sys

NUM, STR, BOOL, BIG = "number", "string", "boolean", "bigint"
NARR, SARR = "number[]", "string[]"
TUP = "[number, string]"
MSN, MNS, SETN = "Map<string, number>", "Map<number, string>", "Set<number>"
NNUM, USTR = "number | null", "string | undefined"
FN = "(x: number) => number"
DATE = "Date"
REC = "Rec"
INTS = ["u32", "i32", "u16", "i16", "u8", "i8"]
INT_RANGE = {"u32": (0, 2**32 - 1), "i32": (-2**31, 2**31 - 1), "u16": (0, 65535), "i16": (-32768, 32767),
             "u8": (0, 255), "i8": (-128, 127)}

NUM_LITS = ["0", "-0", "1", "-1", "2", "3", "5", "7", "10", "16", "100", "255", "256", "1000", "65535",
            "0.1", "0.2", "0.5", "1.5", "2.5", "-2.5", "0.3", "3.14159", "123.456", "1e21", "1e-7", "1.5e300",
            "-1e-300", "5e-324", "1.7976931348623157e308", "9007199254740991", "9007199254740993", "2147483647",
            "2147483648", "4294967295", "4294967296", "-2147483649", "NaN", "Infinity", "-Infinity", "0x1f",
            "0b1011", "0o17", "1_000_000", "0.000001", "1e-6", "123456789.123", "Number.EPSILON",
            "Number.MAX_SAFE_INTEGER", "Number.MIN_SAFE_INTEGER", "Number.MAX_VALUE", "Number.MIN_VALUE",
            "Math.PI", "Math.E", "Math.LN2", "Math.SQRT2", "4.35", "1.005", "0.000001234", "1e15", "1e16",
            "123e-20", "-1.5", "0.5e-6", "1e100", "2e-7", "-0.0000001", "99.995", "8.345", "1.45", "25", "-17",
            "42", "6", "1e300", "12345678901234567890"]
SMALL_INTS = ["0", "1", "2", "3", "4", "5", "-1", "-2", "7", "10"]
STR_LITS = ["", "a", "b", "abc", "hello", "Hello World", "  pad  ", "x,y,z", "a,b,,c", "ünïcödé", "日本語",
            "😀", "a😀b", "Ωμέγα", "Привет", "\\n", "tab\\there", "quote\\\"q", "back\\\\slash", "42", "  42  ",
            "-0", "1e3", "0x1f", "3.75", "abcabc", "ABC", "MiXeD", "$&", "$$", "a$b", "\\u0000", "é", "e\\u0301",
            "Infinity", "NaN", "  ", "--", "aaa", "ba", "😀😀", "\\uD83D", "true", "null", "Zz", "1,2,3", "%41"]
SEP_LITS = ["", ",", ", ", "-", "a", "ab", "😀", " ", "|"]
BIG_LITS = ["0n", "1n", "-1n", "2n", "3n", "7n", "10n", "-5n", "255n", "1000n", "123456789012345678901234567890n",
            "-98765432109876543210n", "18446744073709551615n", "9223372036854775807n", "-9223372036854775808n",
            "4294967296n", "0x1fn", "0b101n", "0o777n", "65537n"]
DATE_STRS = ["2020-01-01", "2020-01-01T00:00:00Z", "2020-02-29T23:59:59.999Z", "1970-01-01T00:00:00.000+02:00",
             "2000-06-15T12:30", "Tue, 01 Jan 2030 10:20:30 GMT", "+010000-01-01T00:00:00Z",
             "-000001-01-01T00:00:00Z", "1999-12-31T23:59:59Z", "2024-07-04T05:06:07.089Z", "1900-03-01T00:00Z"]
BAD_DATE_STRS = ["abc", "", "2020-02-30T25:00:00Z", "2021-13-01"]
MATH1 = ["abs", "floor", "ceil", "round", "trunc", "sign", "sqrt", "cbrt", "exp", "expm1", "log", "log2", "log10",
         "log1p", "sin", "cos", "tan", "atan", "asin", "acos", "sinh", "cosh", "tanh", "asinh", "acosh", "atanh",
         "fround", "clz32"]
# Math functions whose results are exact (correctly rounded) everywhere. The others come from the C library
# in e7 and from fdlibm in V8, and differ in the last bit now and then: unless --exact-math is given, their
# results go through approx() (12 significant digits) so that a 1-ulp difference does not show.
MATH_EXACT = {"abs", "floor", "ceil", "round", "trunc", "sign", "sqrt", "fround", "clz32", "imul", "max", "min"}
ERR_TYPES = ["Error", "RangeError", "TypeError", "SyntaxError", "URIError"]


def tslit(s):
    """A double-quoted TS string literal from a pool entry (pool entries are already escaped)."""
    return '"' + s + '"'


class Cls:
    def __init__(self, name, parent):
        self.name, self.parent = name, parent
        self.fields = []      # (name, type, readonly) own fields, in declaration order
        self.ctor_params = []  # types of all constructor params

    def all_fields(self):
        return (self.parent.all_fields() if self.parent else []) + self.fields

    def chain(self):
        return ([self] + self.parent.chain()) if self.parent else [self]


class Gen:
    def __init__(self, rng, ints=True, exact_math=False):
        self.r = rng
        self.ints = ints
        self.exact_math = exact_math
        self.n = 0
        self.scopes = [[]]   # list of (name, type, mutable)
        self.classes = []
        self.funcs = []      # (name, [param types], ret type)
        self.lines = []
        self.loop_depth = 0
        self.in_fn = False
        # nullable / class variables tested in an enclosing condition: tsc narrows them there (to `never`
        # in a dead branch, where `x?.m()` or `x instanceof C` are errors), so they are used there only in
        # comparisons with null and `??` (vars(narrowed_ok=True))
        self.blocked = set()
        self.guards = [set()]  # per scope: variables blocked by guard() until the scope ends

    # ---------------------------------------------------------------- helpers
    def fresh(self, p="v"):
        # never a bare type name such as i8 / u16 (they are conversion functions)
        self.n += 1
        return f"{p}{self.n}"

    def chance(self, p):
        return self.r.random() < p

    def pick(self, xs):
        return self.r.choice(xs)

    def vars(self, t, mutable=None, narrowed_ok=False):
        out, seen = [], set()
        for sc in reversed(self.scopes):  # innermost first: a shadowed name has the inner type
            for (nm, ty, mu) in reversed(sc):
                if nm in seen:
                    continue
                seen.add(nm)
                if nm in self.blocked and not (narrowed_ok and ty in (NNUM, USTR)):
                    continue
                if ty == t and (mutable is None or mu == mutable):
                    out.append(nm)
        return out

    def var(self, t, narrowed_ok=False):
        vs = self.vars(t, narrowed_ok=narrowed_ok)
        return self.pick(vs) if vs else None

    def declare(self, name, t, mutable):
        self.scopes[-1].append((name, t, mutable))

    def push(self):
        self.scopes.append([])
        self.guards.append(set())

    def pop(self):
        self.scopes.pop()
        self.blocked -= self.guards.pop()

    def guard(self, cond):
        """`cond` guards a jump (`if (cond) break;`): tsc narrows its variables for the rest of the block."""
        add = self.narrowable_in(cond) - self.blocked
        self.blocked |= add
        self.guards[-1] |= add
        return cond

    def lam(self, params, body_fn):
        """Generate an arrow function: params = [(name, type)], body_fn() -> expression string."""
        self.push()
        for (p, t) in params:
            self.declare(p, t, False)
        body = body_fn()
        self.pop()
        return body

    def narrowable_in(self, text):
        """Nullable and class-typed variables mentioned in an expression."""
        out = set()
        for sc in self.scopes:
            for (nm, ty, _) in sc:
                if (ty in (NNUM, USTR) or ty in self.class_types()) and re.search(r"\b" + re.escape(nm) + r"\b", text):
                    out.add(nm)
        return out

    def blocking(self, text, fn):
        """fn() with the narrowable variables of `text` blocked."""
        add = self.narrowable_in(text) - self.blocked
        self.blocked |= add
        try:
            return fn()
        finally:
            self.blocked -= add

    def class_types(self):
        return [c.name for c in self.classes]

    # ---------------------------------------------------------------- expressions
    def num(self, d):
        r = self.r
        v = self.var(NUM)
        if d <= 0 or self.chance(0.2):
            if v and self.chance(0.6):
                return v
            lit = self.pick(NUM_LITS)
            return f"({lit})" if lit.startswith("-") else lit
        k = r.randrange(32)
        if k < 6:
            op = self.pick(["+", "-", "*", "/", "%", "+", "-", "*", "**"])
            a, b = self.num(d - 1), self.num(d - 1)
            if op == "**":
                b = self.pick(["2", "3", "0.5", "-1", "0", "10", "(-0.5)", "NaN", "Infinity"])
            return f"({a} {op} {b})"
        if k == 6:
            op = self.pick(["&", "|", "^", "<<", ">>", ">>>"])
            return f"({self.num(d - 1)} {op} {self.num(d - 1)})"
        if k == 7:
            return self.pick([f"(-{self.num(d - 1)})", f"(~{self.num(d - 1)})"])
        if k in (8, 9):
            f = self.pick(MATH1)
            return self.math(f, f"Math.{f}({self.num(d - 1)})")
        if k == 10:
            f = self.pick(["atan2", "pow", "imul", "max", "min", "hypot"])
            return self.math(f, f"Math.{f}({self.num(d - 1)}, {self.num(d - 1)})")
        if k == 11:
            a = self.var(NARR)
            if a and self.chance(0.5):
                return f"Math.{self.pick(['max', 'min'])}(...{a})"
            f = self.pick(['max', 'min', 'hypot'])
            return self.math(f, f"Math.{f}({self.num(d - 1)}, {self.num(d - 1)}, {self.num(d - 1)})")
        if k == 12:
            # Number(...): with two literals tsc types the conditional as a union of literal types
            c = self.boolean(d - 1)
            return self.blocking(c, lambda: f"Number({c} ? {self.num(d - 1)} : {self.num(d - 1)})")
        if k == 13:
            s = self.string(d - 1)
            return self.pick([f"{s}.length", f"{s}.indexOf({self.string(d - 1)})",
                              f"{s}.lastIndexOf({self.string(d - 1)})", f"{s}.charCodeAt({self.num(d - 1)})",
                              f"({s}.codePointAt({self.small()}) ?? -1)",
                              f"{s}.indexOf({self.string(d - 1)}, {self.small()})"])
        if k == 14:
            s = self.string(d - 1)
            return self.pick([f"parseInt({s})", f"parseFloat({s})", f"Number({s})",
                              f"parseInt({s}, {self.pick(['2', '8', '10', '16', '36', '0', '37'])})",
                              f"Number.parseFloat({s})", f"Number({self.boolean(d - 1)})"])
        if k == 15:
            a = self.narr(d - 1)
            return self.pick([f"{a}.length", f"{a}.indexOf({self.num(d - 1)})",
                              f"{a}.lastIndexOf({self.num(d - 1)})",
                              f"{a}.reduce((acc, x) => acc + x, {self.num(0)})",
                              f"({a}.at({self.small()}) ?? 0)",
                              f"({a}.find((x) => x > {self.num(0)}) ?? -1)",
                              f"{a}.findIndex((x) => x < {self.num(0)})",
                              f"({a}.findLast((x) => x !== {self.num(0).replace('NaN', '0')}) ?? -2)",
                              f"{a}.findLastIndex((x) => x >= {self.num(0)})",
                              f"{a}.reduceRight((acc, x) => acc * 2 + x, 0)"])
        if k == 16:
            a = self.var(NARR)
            if a:
                return f"({a}.length > 0 ? {a}[ix({self.num(d - 1)}, {a}.length)] : 0)"
        if k == 17:
            m = self.var(MSN)
            if m:
                return self.pick([f"{m}.size", f"({m}.get({self.string(d - 1)}) ?? 0)"])
            st = self.var(SETN)
            if st:
                return f"{st}.size"
        if k == 18:
            t = self.var(TUP)
            if t:
                return f"{t}[0]"
            rc = self.var(REC)
            if rc:
                return self.pick([f"{rc}.id", f"({rc}.score ?? -1)", f"{rc}.tags.length"])
        if k == 19:
            e = self.inst_num(d)
            if e:
                return e
        if k == 20:
            e = self.call_fn(NUM, d)
            if e:
                return e
        if k == 21:
            f = self.var(FN)
            if f:
                return f"{f}({self.num(d - 1)})"
        if k == 22:
            n = self.var(NNUM, narrowed_ok=True)
            if n:
                return self.pick([f"({n} ?? {self.num(d - 1)})", f"({n} !== null ? {n} * 2 : -1)"])
        if k == 23:
            dt = self.var(DATE)
            if dt:
                g = self.pick(["getTime", "getUTCFullYear", "getUTCMonth", "getUTCDate", "getUTCDay", "getUTCHours",
                               "getUTCMinutes", "getUTCSeconds", "getUTCMilliseconds", "getFullYear", "getMonth",
                               "getDate", "getDay", "getHours", "getTimezoneOffset", "valueOf"])
                return f"{dt}.{g}()"
            return self.pick([f"Date.UTC({self.date_args()})",
                              f"Date.parse({tslit(self.pick(DATE_STRS + BAD_DATE_STRS))})"])
        if k == 24 and self.ints:
            t = self.pick(INTS)
            return f"Number({self.intx(t, d - 1)})"
        if k == 25:
            b = self.big(d - 1)
            return self.pick([f"Number({b})", f"Number(BigInt.asIntN(32, {b}))"])
        if k == 26:
            a = self.narr(d - 1)
            return f"{a}.map((x) => x * 2).filter((x) => x > 0).length"
        if k == 27:
            s = self.sarr(d - 1)
            return self.pick([f"{s}.length", f"{s}.indexOf({self.string(d - 1)})", f"{s}.join().length"])
        if k == 28:
            c = self.var(FN)
            return f"{self.pick(['Number.MAX_SAFE_INTEGER', 'Number.EPSILON', 'Number.NEGATIVE_INFINITY', 'Number.NaN'])}" if not c else f"[1, 2, 3].map({c}).reduce((a, b) => a + b, 0)"
        if k == 29:
            return f"(Number({self.boolean(d - 1)}) + Number({self.string(d - 1)}.length > 2))"
        if k == 30:
            return f"Math.round({self.num(d - 1)} * 100) / 100"
        v = self.var(NUM)
        if v:
            return v
        lit = self.pick(NUM_LITS[:20])
        return f"({lit})" if lit.startswith("-") else lit

    def math(self, f, call):
        return call if self.exact_math or f in MATH_EXACT else f"approx({call})"

    def small(self):
        return self.pick(SMALL_INTS + ["-3", "8", "NaN", "1.7"])

    def inst_num(self, d):
        insts = [(nm, c) for c in self.classes for nm in self.vars(c.name)]
        if not insts:
            return None
        nm, c = self.pick(insts)
        nf = [f for (f, t, _) in c.all_fields() if t == NUM]
        opts = [f"{nm}.m({self.num(d - 1)})", f"{nm}.total()"]
        opts += [f"{nm}.{f}" for f in nf]
        opts.append(f"{self.pick(c.chain()).name}.count")
        return self.pick(opts)

    def call_fn(self, t, d):
        fs = [f for f in self.funcs if f[2] == t]
        if not fs:
            return None
        nm, ps, _ = self.pick(fs)
        args = []
        for p in ps:
            if p == "rest":
                args += [self.num(d - 1) for _ in range(self.r.randrange(4))]
            elif p == "opt":
                if self.chance(0.5):
                    args.append(self.num(d - 1))
            elif p == "rec":
                args.append(f"ix({self.num(d - 1)}, 16)")
            else:
                args.append(self.gen(p, d - 1))
        return f"{nm}({', '.join(args)})"

    def date_args(self):
        r = self.r
        y = self.pick([str(r.randrange(1960, 2100)), str(r.randrange(0, 100)), str(r.randrange(-500, 10050)), "2000"])
        args = [y, str(r.randrange(-14, 26))]
        for lo, hi in [(-40, 40), (-30, 50), (-70, 130), (-70, 130), (-2000, 3000)][: r.randrange(6)]:
            args.append(str(r.randrange(lo, hi)))
        return ", ".join(args)

    def date(self, d):
        v = self.var(DATE)
        if v and self.chance(0.4):
            return v
        k = self.r.randrange(5)
        if k == 0:
            return f"new Date(Date.UTC({self.date_args()}))"
        if k == 1:
            ms = self.pick(["0", "-1", "86400000", "8.64e15", "-8.64e15", "1e12", "1577836800000", "951782400000.5",
                            "-62198755200000", str(self.r.randrange(-10**13, 10**13))])
            return f"new Date({ms})"
        if k == 2:
            return f"new Date({tslit(self.pick(DATE_STRS))})"
        if k == 3:
            return f"new Date({self.date_args()})"
        if v:
            return f"new Date({v}.getTime() % 1e15 + {self.r.randrange(-10**9, 10**9)})"
        return "new Date(0)"

    def string(self, d):
        r = self.r
        if d <= 0 or self.chance(0.2):
            v = self.var(STR)
            if v and self.chance(0.6):
                return v
            return tslit(self.pick(STR_LITS))
        k = r.randrange(30)
        if k < 3:
            return f"({self.string(d - 1)} + {self.pick([self.string(d - 1), self.num(d - 1), self.boolean(d - 1)])})"
        if k == 3:
            return f"`{self.tmpl_part(d)}-{self.tmpl_part(d)}{self.pick(['', ':', '😀'])}{self.tmpl_part(d)}`"
        if k in (4, 5):
            n = self.num(d - 1)
            m = self.pick([f"toFixed({r.randrange(0, 21)})", f"toFixed({self.pick(['0', '1', '2', '20', '100'])})",
                           f"toPrecision({r.randrange(1, 22)})", f"toExponential({r.randrange(0, 21)})",
                           f"toString({r.randrange(2, 37)})", "toString()", f"toString({self.pick(['2', '16', '36', '3', '7'])})",
                           "toExponential()", "toPrecision()"])
            return f"({n}).{m}"
        if k == 6:
            return f"String({self.pick([self.num(d - 1), self.boolean(d - 1), self.big(d - 1)])})"
        if k in (7, 8, 9):
            s = self.string(d - 1)
            m = self.pick([f"slice({self.num(d - 1)})", f"slice({self.small()}, {self.small()})",
                           f"substring({self.small()}, {self.num(d - 1)})", f"substr({self.small()}, {self.small()})",
                           "toUpperCase()", "toLowerCase()", "trim()", "trimStart()", "trimEnd()",
                           f"padStart({r.randrange(0, 25)}, {self.string(0)})", f"padEnd({r.randrange(0, 25)})",
                           f"repeat({r.randrange(0, 4)})", f"concat({self.string(d - 1)}, {self.string(0)})",
                           f"replace({tslit(self.pick(STR_LITS))}, {tslit(self.pick(STR_LITS))})",
                           f"replaceAll({tslit(self.pick(STR_LITS))}, {tslit(self.pick(STR_LITS))})",
                           f"charAt({self.num(d - 1)})", f"split({tslit(self.pick(SEP_LITS))}).join({tslit(self.pick(SEP_LITS))})",
                           f"split({tslit(self.pick(SEP_LITS))}, {r.randrange(0, 4)}).join(\"|\")",
                           f"slice({self.small()}, {self.small()}).toUpperCase()"])
            return f"{s}.{m}"
        if k == 10:
            s = self.string(d - 1)
            if not re.fullmatch(r"[A-Za-z_]\w*", s):  # indexed below: evaluated more than once
                return f"({s}.at({self.small()}) ?? \"?\")"
            return self.pick([f"({s}.at({self.small()}) ?? \"?\")",
                              f"({s}.length > 0 ? {s}[ix({self.num(d - 1)}, {s}.length)] : \"\")"])
        if k == 11:
            return f"{self.narr(d - 1)}.join({tslit(self.pick(SEP_LITS))})"
        if k == 12:
            return f"{self.sarr(d - 1)}.join({self.pick([tslit(self.pick(SEP_LITS)), ''])})"
        if k in (13, 14):
            return self.json(d - 1)
        if k == 15:
            b = self.big(d - 1)
            return self.pick([f"{b}.toString()", f"({b}).toString({self.pick(['2', '16', '36', '7'])})", f"String({b})"])
        if k == 16:
            return self.pick([f"String.fromCharCode({self.num(d - 1)})",
                              f"String.fromCharCode({self.num(d - 1)}, {self.small()})",
                              f"String.fromCodePoint({self.pick(['65', '0x1F600', '0x10FFFF', '0', '233', '0xD800'])})"])
        if k == 17:
            lit = tslit(self.pick([s for s in STR_LITS if "uD83D" not in s]))
            return self.pick([f"encodeURIComponent({lit})", f"decodeURIComponent(encodeURIComponent({lit}))",
                              f"encodeURI({lit})", f"decodeURIComponent({tslit(self.pick(['%41', '%C3%A9', 'a%20b', '%F0%9F%98%80']))})"])
        if k == 18:
            dt = self.date(d - 1)
            m = self.pick(["toISOString", "toJSON", "toString", "toUTCString", "toDateString", "toTimeString"])
            return f"{dt}.{m}()"
        if k == 19:
            vs = self.all_nonnull_vars()
            if vs:
                v = self.pick(vs)
                return f"String(typeof {v})"
        if k == 20:
            u = self.var(USTR)
            if u:
                return self.pick([f"({u} ?? \"none\")", f"({u}?.toUpperCase() ?? \"-\")", f"String({u}?.length)"])
            u = self.var(USTR, narrowed_ok=True)
            if u:
                return f"({u} ?? \"none\")"
            n = self.var(NNUM)
            if n:
                return f"({n}?.toFixed(2) ?? \"null\")"
        if k == 21:
            insts = [(nm, c) for c in self.classes for nm in self.vars(c.name)]
            if insts:
                nm, c = self.pick(insts)
                return self.pick([f"{nm}.describe()", f"{nm}.label", f"{nm}.kind()"])
        if k == 22:
            e = self.call_fn(STR, d)
            if e:
                return e
        if k == 23:
            t = self.var(TUP)
            if t:
                return f"{t}[1]"
            rc = self.var(REC)
            if rc:
                return f"{rc}.label"
        if k == 24 and self.ints:
            t = self.pick(INTS)
            return self.pick([f"String({self.intx(t, d - 1)})", f"`${{{self.intx(t, d - 1)}}}`"])
        if k == 25:
            m = self.var(MNS)
            if m:
                return f"({m}.get({self.num(d - 1)}) ?? \"\")"
        if k == 26:
            c = self.boolean(d - 1)
            return self.blocking(c, lambda: f"String({c} ? {self.string(d - 1)} : {self.string(d - 1)})")
        if k == 27:
            s = self.string(d - 1)
            return self.pick([f"[...{s}].reverse().join(\"\")", f"Array.from({s}).join(\".\")",
                              f"{s}.split(\"\").reverse().join(\"\")"])
        v = self.var(STR)
        return v if v else tslit(self.pick(STR_LITS))

    def all_nonnull_vars(self):
        return [nm for sc in self.scopes for (nm, ty, _) in sc
                if ty in (NUM, STR, BOOL, BIG, NARR, SARR, FN) and not nm.startswith("this.")]

    def tmpl_part(self, d):
        e = self.pick([self.string(d - 1), self.num(d - 1), self.boolean(d - 1), self.big(d - 1)])
        return "${" + e + "}"

    def boolean(self, d):
        r = self.r
        if d <= 0 or self.chance(0.15):
            v = self.var(BOOL)
            if v and self.chance(0.6):
                return v
            return self.pick([f"({self.num(0)} < {self.num(0)})", f"Boolean({self.num(0)})", f"({self.num(0)} >= {self.small()})"])
        k = r.randrange(20)
        if k < 4:
            op = self.pick(["<", "<=", ">", ">=", "===", "!=="])
            a, b = self.num(d - 1), self.num(d - 1)
            if op in ("===", "!=="):
                a = f"Number({a})"
                if b in ("NaN", "Number.NaN"):  # tsc: "this condition will always return true"
                    b = "Number(\"x\")"
            return f"({a} {op} {b})"
        if k == 4:
            op = self.pick(["<", ">", "===", "!==", "<="])
            a, b = self.string(d - 1), self.string(d - 1)
            if op in ("===", "!=="):
                a = f"String({a})"
            return f"({a} {op} {b})"
        if k == 5:
            return f"(!{self.boolean(d - 1)})"
        if k == 6:
            a = self.boolean(d - 1)
            return f"({a} {self.pick(['&&', '||'])} {self.blocking(a, lambda: self.boolean(d - 1))})"
        if k == 7:
            f = self.pick(["Number.isInteger", "Number.isSafeInteger", "Number.isFinite", "Number.isNaN", "isNaN",
                           "isFinite", "Boolean"])
            return f"{f}({self.num(d - 1)})"
        if k == 8:
            s = self.string(d - 1)
            m = self.pick(["includes", "startsWith", "endsWith"])
            if self.chance(0.3):
                return f"{s}.{m}({self.string(d - 1)}, {self.small()})"
            return f"{s}.{m}({self.string(d - 1)})"
        if k == 9:
            a = self.narr(d - 1)
            return self.pick([f"{a}.includes({self.num(d - 1)})", f"{a}.some((x) => x > {self.num(0)})",
                              f"{a}.every((x) => x === x)", f"{a}.every((x, i) => x >= i)"])
        if k == 10:
            st = self.var(SETN)
            if st:
                return f"{st}.has({self.num(d - 1)})"
            m = self.var(MSN)
            if m:
                return f"{m}.has({self.string(d - 1)})"
        if k == 11:
            insts = [(nm, c) for c in self.classes for nm in self.vars(c.name)]
            if insts:
                nm, c = self.pick(insts)
                return f"({nm} instanceof {self.pick(self.classes).name})"
        if k == 12:
            op = self.pick(["<", "<=", ">", ">=", "===", "!=="])
            a, b = self.big(d - 1), self.big(d - 1)
            if op in ("===", "!=="):
                a = f"({a} + 0n)"
            return f"({a} {op} {b})"
        if k == 13:
            n = self.var(NNUM, narrowed_ok=True)
            if n:
                return self.pick([f"({n} === null)", f"({n} !== null && {n} > 0)", f"({n} == null)",
                                  f"(null !== {n})", f"({n} != undefined)"])
            u = self.var(USTR, narrowed_ok=True)
            if u:
                return self.pick([f"({u} !== undefined)", f"({u} === undefined || {u} === null)"])
        if k == 14:
            e = self.call_fn(BOOL, d)
            if e:
                return e
        if k == 15 and self.ints:
            t = self.pick(INTS)
            a, b = self.intx(t, d - 1), self.intx(t, d - 1)
            return f"({a} {self.pick(['<', '>', '===', '!==', '<='])} {b})"
        if k == 16:
            return f"Boolean({self.string(d - 1)})"
        if k == 17:
            s = self.sarr(d - 1)
            return f"{s}.includes({self.string(d - 1)})"
        v = self.var(BOOL)
        return v if v else f"({self.num(0)} <= {self.num(0)})"

    def big(self, d):
        r = self.r
        if d <= 0 or self.chance(0.25):
            v = self.var(BIG)
            if v and self.chance(0.6):
                return v
            lit = self.pick(BIG_LITS)
            return f"({lit})" if lit.startswith("-") else lit
        k = r.randrange(12)
        a = self.big(d - 1)
        if k < 3:
            return f"({a} {self.pick(['+', '-', '*'])} {self.big(d - 1)})"
        if k == 3:
            b = self.big(d - 1)
            return f"({a} {self.pick(['/', '%'])} nz({b}))"
        if k == 4:
            return f"({a} {self.pick(['&', '|', '^'])} {self.big(d - 1)})"
        if k == 5:
            return f"({a} {self.pick(['<<', '>>'])} {r.randrange(0, 70)}n)"
        if k == 6:
            return self.pick([f"(-{a})", f"(~{a})"])
        if k == 7:
            return f"({self.pick(['2n', '3n', '10n', '(-7n)', '255n'])} ** {r.randrange(0, 40)}n)"
        if k == 8:
            return f"BigInt.{self.pick(['asIntN', 'asUintN'])}({self.pick(['0', '1', '8', '32', '63', '64', '65', '128'])}, {a})"
        if k == 9:
            return self.pick([f"BigInt({self.pick(['0', '42', '-7', '9007199254740993', '1e20', '2 ** 64'])})",
                              f"BigInt({tslit(self.pick(['123', '-45', '0x1f', '  42  ', '0b11', '', '99999999999999999999']))})",
                              f"toBig({self.num(d - 1)})"])
        if k == 10 and self.ints:
            t = self.pick(INTS)
            return f"BigInt({self.intx(t, d - 1)})"
        return a

    def intx(self, t, d):
        """Fixed-width integer expression of type t; every operation is wrapped in t(...)."""
        r = self.r
        lo, hi = INT_RANGE[t]
        if d <= 0 or self.chance(0.25):
            v = self.var(t)
            if v and self.chance(0.6):
                return v
            x = self.pick([lo, hi, 0, 1, r.randrange(lo, hi + 1), r.randrange(lo, hi + 1)])
            # a bare literal takes its type from context; give it one
            return f"{t}({x})"
        k = r.randrange(10)
        a = self.intx(t, d - 1)
        if k < 3:
            ops = ["+", "-", "&", "|", "^"]
            if t in ("u16", "i16", "u8", "i8"):
                ops.append("*")
            return f"{t}({a} {self.pick(ops)} {self.intx(t, d - 1)})"
        if k == 3:
            b = self.intx(t, d - 1)
            return f"{t}({a} {self.pick(['/', '%'])} {t}({b} | {t}(1)))"
        if k == 4:
            sh = self.pick([">>>", "<<"] if t[0] == "u" else [">>", "<<", ">>"])
            return f"{t}({a} {sh} {r.randrange(0, 32)})"
        if k == 5:
            return f"{t}(~{a})"
        if k == 6:
            return f"{t}({self.num(d - 1)})"
        if k == 7:
            o = self.pick(INTS)
            return f"{t}({self.intx(o, d - 1)})"
        if k == 8:
            return f"{t}({self.big(d - 1)})"
        if k == 9 and t == "u32":
            return self.pick([f"u32(popcnt({a}))", f"u32(clz32({a}))", f"u32(ctz32({a}))",
                              f"rotl32({a}, {r.randrange(0, 40)})", f"rotr32({a}, {r.randrange(0, 40)})"])
        return a

    def narr(self, d):
        r = self.r
        v = self.var(NARR)
        if d <= 0 or self.chance(0.3):
            if v and self.chance(0.7):
                return v
            n = r.randrange(0, 5)
            return "[" + ", ".join(self.num(0) for _ in range(n)) + "]" if n else "([] as number[])"
        k = r.randrange(14)
        a = self.narr(d - 1)
        if k < 2:
            return self.lam([("x", NUM)], lambda: f"{a}.map((x) => {self.num(d - 1)})")
        if k == 2:
            return self.lam([("x", NUM), ("i", NUM)], lambda: f"{a}.map((x, i) => {self.num(d - 1)})")
        if k == 3:
            return self.lam([("x", NUM)], lambda: f"{a}.filter((x) => {self.boolean(d - 1)})")
        if k == 4:
            return f"{a}.slice({self.small()}{', ' + self.small() if self.chance(0.6) else ''})"
        if k == 5:
            return self.pick([f"{a}.concat({self.narr(d - 1)})", f"[...{a}, {self.num(d - 1)}, ...{self.narr(d - 1)}]"])
        if k == 6:
            return self.pick([f"[...{a}].sort(cmpNum)", f"[...{a}].sort((p, q) => cmpNum(q, p))", f"[...{a}].sort()",
                              f"{a}.slice().reverse()"])
        if k == 7:
            n = r.randrange(0, 7)
            return self.lam([("i", NUM)], lambda: f"Array.from({{ length: {n} }}, (_, i) => {self.num(d - 1)})")
        if k == 8:
            return f"Array.of({', '.join(self.num(d - 1) for _ in range(r.randrange(1, 4)))})"
        if k == 9:
            st = self.var(SETN)
            if st:
                return self.pick([f"[...{st}]", f"Array.from({st})"])
            m = self.var(MSN)
            if m:
                return f"[...{m}.values()]"
        if k == 10:
            return f"{self.sarr(d - 1)}.map((x) => x.length)"
        if k == 11:
            return f"new Array<number>({r.randrange(0, 5)}).fill({self.num(d - 1)})"
        if k == 12:
            return f"{a}.slice().fill({self.num(d - 1)}, {self.small()}, {self.small()})"
        f = self.var(FN)
        if f:
            return f"{a}.map((x) => {f}(x))"
        return a

    def sarr(self, d):
        r = self.r
        v = self.var(SARR)
        if d <= 0 or self.chance(0.3):
            if v and self.chance(0.7):
                return v
            n = r.randrange(0, 5)
            return "[" + ", ".join(self.string(0) for _ in range(n)) + "]" if n else "([] as string[])"
        k = r.randrange(9)
        a = self.sarr(d - 1)
        if k < 2:
            return f"{self.string(d - 1)}.split({tslit(self.pick(SEP_LITS))})"
        if k == 2:
            src = self.narr(d - 1)
            return self.lam([("x", NUM)], lambda: f"{src}.map((x) => {self.string(d - 1)})")
        if k == 3:
            return self.lam([("x", STR)], lambda: f"{a}.filter((x) => {self.boolean(d - 1)})")
        if k == 4:
            return self.pick([f"[...{a}].sort()", f"{a}.slice({self.small()})", f"{a}.concat({self.sarr(d - 1)})"])
        if k == 5:
            rc = self.var(REC)
            if rc:  # not Object.keys(rc): e7 leaves out a property whose value is undefined (`score: undefined`)
                return self.pick([f"Object.keys({{ a: {self.num(d - 1)}, b: {rc}.label }})", f"{rc}.tags"])
            m = self.var(MSN)
            if m:
                return f"[...{m}.keys()]"
        if k == 6:
            return self.lam([("x", STR)], lambda: f"{a}.map((x) => {self.string(d - 1)})")
        if k == 7:
            return f"[...{a}].sort((p, q) => (p < q ? 1 : p > q ? -1 : 0))"
        return a

    def json(self, d):
        """JSON.stringify of some value."""
        t = self.pick([NARR, SARR, TUP, REC, MSN, SETN, NUM, STR, BOOL, NNUM, DATE, "inst", "obj", "nested"])
        if t == "inst":
            insts = [nm for c in self.classes for nm in self.vars(c.name)]
            v = self.pick(insts) if insts else self.narr(d)
        elif t == "obj":
            v = f"{{ a: {self.num(d)}, b: {self.string(d)}, c: [{self.boolean(d)}, null], d: {{ e: {self.narr(d)} }} }}"
        elif t == "nested":
            v = self.pick([f"[{self.narr(d)}, {self.narr(d)}]", f"[[{self.sarr(d)}]]"])
        elif t == MSN:
            m = self.var(MSN)
            v = self.pick([f"[...{m}]", f"[...{m}.entries()]", m]) if m else "[0]"
        elif t == SETN:
            st = self.var(SETN)
            v = self.pick([f"[...{st}]", st]) if st else "[1]"
        elif t in (NNUM, DATE):
            v = self.var(t) or self.num(d)
        elif t == REC:
            v = self.rec(d, typed=False)
        else:
            v = self.gen(t, d)
        if self.chance(0.2):
            return f"JSON.stringify({v}, null, {self.pick(['2', '0', '1', '4', tslit('--'), '12'])})"
        return f"JSON.stringify({v})"

    def tup(self, d):
        v = self.var(TUP)
        if v and self.chance(0.4):
            return v
        return f"([{self.num(d)}, {self.string(d)}] as [number, string])"

    def rec(self, d, typed=True):
        """An object literal of interface Rec; `typed`: in a position whose type is Rec (e7 cannot infer the
        type of `score: undefined` without one)."""
        v = self.var(REC)
        if v and self.chance(0.3):
            return f"{{ ...{v}, id: {self.num(d)} }}"
        parts = [f"id: {self.num(d)}", f"label: {self.string(d)}"]
        # `score` may be left out: a property added later goes to the end of the key order in JavaScript (and
        # in e7's layout of the literal, while only one optional property is missing)
        if self.chance(0.75):
            parts.append(f"score: {self.pick([self.num(d), 'undefined'] if typed else [self.num(d)])}")
        parts.append(f"tags: {self.sarr(d)}")
        if self.chance(0.3):
            self.r.shuffle(parts)
        return "{ " + ", ".join(parts) + " }"

    def gen(self, t, d):
        if t == NUM:
            return self.num(d)
        if t == STR:
            return self.string(d)
        if t == BOOL:
            return self.boolean(d)
        if t == BIG:
            return self.big(d)
        if t == NARR:
            return self.narr(d)
        if t == SARR:
            return self.sarr(d)
        if t == TUP:
            return self.tup(d)
        if t == REC:
            return self.rec(d)
        if t == DATE:
            return self.date(d)
        if t == MSN:
            entries = ", ".join(f"[{self.string(0)}, {self.num(d - 1)}]" for _ in range(self.r.randrange(0, 4)))
            v = self.var(MSN)
            return self.pick([f"new Map<string, number>([{entries}])", "new Map<string, number>()"] +
                             ([f"new Map<string, number>({v})"] if v else []))
        if t == MNS:
            entries = ", ".join(f"[{self.num(0)}, {self.string(0)}]" for _ in range(self.r.randrange(0, 4)))
            return f"new Map<number, string>([{entries}])"
        if t == SETN:
            return self.pick([f"new Set<number>({self.narr(max(d - 1, 0))})", "new Set<number>()"])
        if t == NNUM:  # always through a call: a literal initializer would narrow the declared type in tsc
            return f"maybe({self.num(d - 1)})"
        if t == USTR:
            return f"maybeStr({self.string(d - 1)})"
        if t == FN:
            k = self.r.randrange(3)
            if k == 0:
                return f"makeAdder({self.num(d - 1)})"
            if k == 1:
                return self.lam([("x", NUM)], lambda: f"(x: number) => {self.num(d - 1)}")
            v = self.var(FN)
            if v:
                return f"compose({v}, makeAdder({self.num(d - 1)}))"
            return "(x: number) => x + 1"
        if t in INTS:
            return self.intx(t, d)
        for c in self.classes:
            if c.name == t:
                sub = self.pick([k for k in self.classes if c in k.chain()])
                return self.new_inst(sub, d)
        raise ValueError(t)

    def new_inst(self, c, d):
        if self.chance(0.15) and c.parent is None:
            return f"{c.name}.make({self.num(d - 1)})"
        return f"new {c.name}({', '.join(self.gen(t, d - 1) for t in c.ctor_params)})"

    # ---------------------------------------------------------------- statements
    def emit(self, ind, s):
        self.lines.append("  " * ind + s)

    def decl_types(self):
        ts = [NUM, NUM, NUM, STR, STR, STR, BOOL, BIG, BIG, NARR, NARR, SARR, TUP, REC, MSN, MNS, SETN, NNUM, USTR,
              FN, DATE]
        if self.ints:
            ts += INTS
        ts += self.class_types() * 2
        return ts

    def stmt_decl(self, ind, d):
        t = self.pick(self.decl_types())
        mutable = self.chance(0.6)
        nm = self.fresh()
        e = self.gen(t, d)
        self.emit(ind, f"{'let' if mutable else 'const'} {nm}: {t} = {e};")
        self.declare(nm, t, mutable)

    def stmt_print(self, ind, d):
        k = self.r.randrange(10)
        if k < 3:
            e = self.num(d)
        elif k < 5:
            e = self.string(d)
        elif k == 5:
            e = self.boolean(d)
        elif k == 6:
            e = self.big(d)
        elif k == 7 and self.ints:
            e = self.intx(self.pick(INTS), d)
        elif k == 8:
            e = self.json(d)
        else:
            parts = [self.pick([self.num, self.string, self.boolean, self.big])(d - 1) for _ in range(self.r.randrange(2, 4))]
            e = ", ".join(parts)
        self.emit(ind, f"console.log({e});")

    def stmt_mutate(self, ind, d):
        cands = [(nm, ty) for sc in self.scopes for (nm, ty, mu) in sc if mu and nm not in self.blocked]
        objs = [(nm, ty) for sc in self.scopes for (nm, ty, mu) in sc if nm not in self.blocked and
                (ty in (NARR, SARR, MSN, MNS, SETN, TUP, REC) or ty in self.class_types())]
        if not cands and not objs:
            return self.stmt_print(ind, d)
        if objs and (not cands or self.chance(0.5)):
            nm, ty = self.pick(objs)
            return self.mutate_obj(ind, nm, ty, d)
        nm, ty = self.pick(cands)
        r = self.r
        if ty == NUM:
            s = self.pick([f"{nm} = {self.num(d)};", f"{nm} += {self.num(d)};", f"{nm} -= {self.num(d)};",
                           f"{nm} *= {self.num(d)};", f"{nm}++;", f"{nm}--;", f"{nm} %= {self.num(d)};",
                           f"{nm} /= {self.num(d)};", f"{nm} **= {self.pick(['2', '0.5', '-1'])};",
                           f"{nm} |= {self.num(d)};", f"{nm} <<= {self.small()};", f"{nm} >>>= {self.small()};"])
        elif ty == STR:
            s = self.pick([f"{nm} = ({self.string(d)}).slice(0, 40);", f"{nm} += ({self.string(d)}).slice(0, 8);"])
        elif ty == BOOL:
            s = self.pick([f"{nm} = !{nm};", f"{nm} = {self.boolean(d)};"])
        elif ty == BIG:
            s = self.pick([f"{nm} = BigInt.asIntN(256, {self.big(d)});",
                           f"{nm} = BigInt.asIntN(200, {nm} {self.pick(['+', '-', '*', '^'])} {self.big(d)});"])
        elif ty == NNUM:
            # `x ??= v` narrows x to number for the rest of the scope (later `x !== null` tests are fine); in v,
            # x is null (blocked)
            t = self.fresh("t")
            s = self.pick([f"{nm} = maybe(-1);", f"{nm} = maybe({self.num(d)});", f"{nm} ??= {self.blocking(nm, lambda: self.num(d))};",
                           f"{{ let {t}: number | null = {nm}; {t} ??= {self.num(d)}; console.log({t}); }}"])
        elif ty == USTR:
            t = self.fresh("t")
            s = self.pick([f"{nm} = maybeStr(\"\");", f"{nm} ??= {self.blocking(nm, lambda: self.string(d))};",
                           f"{{ let {t}: string | undefined = {nm}; {t} ??= {self.string(d)}; console.log({t}); }}"])
        elif ty in INTS:
            s = self.pick([f"{nm} = {self.intx(ty, d)};", f"{nm} = {ty}({nm} + {self.intx(ty, d)});"])
        elif ty == NARR:
            s = f"{nm} = {self.narr(d)}.slice(0, 12);"
        elif ty == SARR:
            s = f"{nm} = {self.sarr(d)}.slice(0, 12);"
        elif ty == FN:
            s = f"{nm} = {self.gen(FN, 1)};"
        elif ty == DATE:
            s = f"{nm} = {self.date(d)};"
        else:
            s = f"{nm} = {self.gen(ty, d)};"
        self.emit(ind, s)

    def mutate_obj(self, ind, nm, ty, d):
        r = self.r
        if ty == NARR:
            s = self.pick([f"{nm}.push({self.num(d)});", f"{nm}.push({self.num(d)}, {self.num(0)});", f"{nm}.pop();",
                           f"{nm}.shift();", f"{nm}.unshift({self.num(d)});", f"{nm}.reverse();", f"{nm}.sort(cmpNum);",
                           f"{nm}.sort();", f"{nm}.splice({self.small()}, {self.small()});",
                           f"{nm}.splice({self.small()}, {self.small()}, {self.num(d)}, {self.num(0)});",
                           f"{nm}.fill({self.num(d)}, {self.small()});",
                           f"if ({nm}.length > 0) {nm}[ix({self.num(d)}, {nm}.length)] = {self.num(d)};",
                           f"{nm}[{nm}.length] = {self.num(d)};",
                           f"{nm}.length = Math.min({nm}.length, {r.randrange(0, 6)});",
                           f"console.log({nm}.splice({self.small()}, {self.small()}).join(\",\"));",
                           f"console.log({nm}.push({self.num(d)}), {nm}.length);"])
            # bounded in loops (a for...of over the array itself would not end)
            if self.loop_depth > 0 and ("push" in s or "unshift" in s or f"{nm}[{nm}.length]" in s):
                s = f"if ({nm}.length < 40) {s}"
        elif ty == SARR:
            s = self.pick([f"{nm}.push({self.string(d)});", f"{nm}.pop();", f"{nm}.sort();", f"{nm}.reverse();",
                           f"{nm}.splice({self.small()}, 1, {self.string(d)});", f"{nm}.unshift({self.string(0)});"])
            if self.loop_depth > 0 and ("push" in s or "unshift" in s):
                s = f"if ({nm}.length < 40) {s}"
        elif ty == MSN:
            s = self.pick([f"{nm}.set({self.string(d)}, {self.num(d)});", f"{nm}.delete({self.string(d)});",
                           f"{nm}.set({self.string(0)}, ({nm}.get({self.string(0)}) ?? 0) + 1);",
                           f"{nm}.clear();", f"console.log({nm}.delete({self.string(d)}), {nm}.size);"])
        elif ty == MNS:
            s = self.pick([f"{nm}.set({self.num(d)}, {self.string(d)});", f"{nm}.delete({self.num(d)});",
                           f"{nm}.set(-0, \"negzero\");", f"console.log({nm}.has({self.num(d)}));"])
        elif ty == SETN:
            s = self.pick([f"{nm}.add({self.num(d)});", f"{nm}.delete({self.num(d)});", f"{nm}.add(NaN);",
                           f"{nm}.add(-0);", f"console.log({nm}.has({self.num(d)}), {nm}.size);"])
        elif ty == TUP:
            s = self.pick([f"{nm}[0] = {self.num(d)};", f"{nm}[1] = {self.string(d)}.slice(0, 20);"])
        elif ty == REC:
            s = self.pick([f"{nm}.id = {self.num(d)};", f"{nm}.score = {self.num(d)};", f"{nm}.tags.push({self.string(0)});"
                           if self.loop_depth == 0 else f"{nm}.label = {self.string(d)}.slice(0, 20);"])
        else:
            c = next(c for c in self.classes if c.name == ty)
            muts = [f for (f, t, ro) in c.all_fields() if not ro and t == NUM]
            opts = [f"{nm}.bump({self.num(d)});"] + [f"{nm}.{f} = {self.num(d)};" for f in muts]
            s = self.pick(opts)
        self.emit(ind, s)

    def body(self, ind, d, n):
        self.push()
        for _ in range(n):
            self.stmt(ind, d)
        self.pop()

    def stmt_if(self, ind, d):
        c = self.boolean(d)
        self.blocking(c, lambda: self.if_rest(ind, d, c))

    def if_rest(self, ind, d, c):
        self.emit(ind, f"if ({c}) {{")
        self.body(ind + 1, d, self.r.randrange(1, 3))
        k = self.r.randrange(3)
        if k == 1:
            self.emit(ind, "} else {")
            self.body(ind + 1, d, self.r.randrange(1, 3))
        elif k == 2:
            c2 = self.boolean(d)
            self.emit(ind, f"}} else if ({c2}) {{")

            def rest():
                self.body(ind + 1, d, self.r.randrange(1, 3))
                self.emit(ind, "} else {")
                self.body(ind + 1, d, 1)
            self.blocking(c2, rest)
        self.emit(ind, "}")

    def loop_body(self, ind, d, n):
        self.loop_depth += 1
        self.push()
        for _ in range(n):
            if self.chance(0.12):
                self.emit(ind, f"if ({self.guard(self.boolean(d - 1))}) {self.pick(['break', 'continue'])};")
            self.stmt(ind, d)
        self.pop()
        self.loop_depth -= 1

    def stmt_loop(self, ind, d):
        r = self.r
        k = r.randrange(7)
        nloop = 3 if self.loop_depth > 0 else 6
        if k == 0:
            i = self.fresh("it")
            self.emit(ind, f"for (let {i} = 0; {i} < {r.randrange(0, nloop + 1)}; {i}++) {{")
            self.push()
            self.declare(i, NUM, False)
            self.loop_body(ind + 1, d, r.randrange(1, 4))
            self.pop()
            self.emit(ind, "}")
        elif k == 1:
            w = self.fresh("w")
            self.emit(ind, f"let {w} = 0;")
            self.declare(w, NUM, False)
            if self.chance(0.5):
                # the counter update is on the header line: the minimizer cannot remove it alone
                self.emit(ind, f"while ({w} < {r.randrange(0, nloop + 1)}) {{ {w}++;")
                self.loop_body(ind + 1, d, r.randrange(1, 3))
                self.emit(ind, "}")
            else:
                self.emit(ind, f"do {{ {w}++;")
                self.loop_body(ind + 1, d, r.randrange(1, 3))
                self.emit(ind, f"}} while ({w} < {r.randrange(0, nloop + 1)});")
        elif k == 2:
            x = self.fresh("x")
            src_t = self.pick([NARR, SARR, STR, MSN, SETN, "entries", MNS])
            self.push()
            if src_t == NARR:
                self.emit(ind, f"for (const {x} of {self.narr(d - 1)}) {{")
                self.declare(x, NUM, False)
            elif src_t == SARR:
                self.emit(ind, f"for (const {x} of {self.sarr(d - 1)}) {{")
                self.declare(x, STR, False)
            elif src_t == STR:
                # bounded: the body may append to the string variable (growing it 9-fold per outer iteration)
                self.emit(ind, f"for (const {x} of ({self.string(d - 1)}).slice(0, 16)) {{")
                self.declare(x, STR, False)
            elif src_t in (MSN, MNS):
                m = self.var(src_t) or self.gen(src_t, 1)
                kk, vv = self.fresh("k"), self.fresh("e")
                self.emit(ind, f"for (const [{kk}, {vv}] of {m}) {{")
                self.declare(kk, STR if src_t == MSN else NUM, False)
                self.declare(vv, NUM if src_t == MSN else STR, False)
            elif src_t == SETN:
                st = self.var(SETN) or "new Set<number>([3, 1, 3, NaN, -0, 0])"
                self.emit(ind, f"for (const {x} of {st}) {{")
                self.declare(x, NUM, False)
            else:
                kk = self.fresh("j")
                self.emit(ind, f"for (const [{kk}, {x}] of {self.narr(d - 1)}.entries()) {{")
                self.declare(kk, NUM, False)
                self.declare(x, NUM, False)
            self.loop_body(ind + 1, d, r.randrange(1, 3))
            self.pop()
            self.emit(ind, "}")
        elif k == 3:
            lab, i, j = self.fresh("outer"), self.fresh("it"), self.fresh("j")
            self.emit(ind, f"{lab}: for (let {i} = 0; {i} < {r.randrange(1, 5)}; {i}++) {{")
            self.push()
            self.declare(i, NUM, False)
            self.emit(ind + 1, f"for (let {j} = 0; {j} < {r.randrange(1, 5)}; {j}++) {{")
            self.push()
            self.declare(j, NUM, False)
            self.loop_depth += 2
            self.emit(ind + 2, f"if ({self.guard(self.boolean(1))}) continue {lab};")
            self.emit(ind + 2, f"if ({i} + {j} > {r.randrange(1, 6)}) break {lab};")
            self.body(ind + 2, d - 1, r.randrange(1, 3))
            self.loop_depth -= 2
            self.pop()
            self.emit(ind + 1, "}")
            self.pop()
            self.emit(ind, "}")
        elif k == 4:
            # closures capturing a per-iteration binding
            fs, i = self.fresh("fns"), self.fresh("it")
            self.emit(ind, f"const {fs}: (() => number)[] = [];")
            self.emit(ind, f"for (let {i} = 0; {i} < {r.randrange(1, 5)}; {i}++) {{")
            self.push()
            self.declare(i, NUM, False)
            self.emit(ind + 1, f"{fs}.push(() => {self.num(1)} + {i});")
            self.pop()
            self.emit(ind, "}")
            self.emit(ind, f"console.log({fs}.map((f) => f()).join(\" \"));")
        elif k == 5:
            lab = self.fresh("blk")
            self.emit(ind, f"{lab}: {{")
            self.push()
            self.body(ind + 1, d - 1, 1)
            self.emit(ind + 1, f"if ({self.guard(self.boolean(1))}) break {lab};")
            self.body(ind + 1, d - 1, 1)
            self.pop()
            self.emit(ind, "}")
        else:
            self.stmt_switch(ind, d)

    def stmt_switch(self, ind, d):
        r = self.r
        if self.chance(0.6):
            self.emit(ind, f"switch (ix({self.num(d - 1)}, 5)) {{")
            labels = r.sample(["0", "1", "2", "3", "4"], r.randrange(1, 5))
        else:
            self.emit(ind, f"switch (String({self.string(d - 1)})) {{")
            labels = list(dict.fromkeys(tslit(self.pick(STR_LITS)) for _ in range(r.randrange(1, 4))))
        for lab in labels:
            self.emit(ind + 1, f"case {lab}: {{")
            self.body(ind + 2, d - 1, r.randrange(0, 2))
            if self.chance(0.75):
                self.emit(ind + 2, "break;")
            self.emit(ind + 1, "}")
        if self.chance(0.6):
            self.emit(ind + 1, "default: {")
            self.body(ind + 2, d - 1, 1)
            self.emit(ind + 1, "}")
        self.emit(ind, "}")

    def stmt_try(self, ind, d):
        r = self.r
        e = self.fresh("e")
        self.emit(ind, "try {")
        self.push()
        self.body(ind + 1, d - 1, r.randrange(0, 2))
        k = r.randrange(6)
        lib_err = False
        if k < 2:
            et = self.pick(ERR_TYPES)
            self.emit(ind + 1, f"if ({self.guard(self.boolean(d - 1))}) throw new {et}({self.string(d - 1)});")
        elif k == 2:
            lib_err = True
            self.emit(ind + 1, "console.log(" + self.pick([
                f"({self.num(1)}).toFixed({self.pick(['101', '-1', '200'])})",
                f"({self.num(1)}).toString({self.pick(['1', '37', '0'])})",
                f"({self.num(1)}).toPrecision({self.pick(['0', '101'])})",
                f"\"ab\".repeat({self.pick(['-1', 'Infinity'])})",
                f"JSON.stringify(JSON.parse({tslit(self.pick(['{bad', '[1,', '', '01', 'tru', '[1]x']))}) as number[])",
                f"{self.big(1)} / 0n", f"{self.big(1)} % 0n",
                "decodeURIComponent(\"%\")", "decodeURIComponent(\"%E0%A4%A\")",
                f"BigInt({self.pick(['1.5', 'NaN', 'Infinity'])})",
                f"BigInt({tslit(self.pick(['1.5', 'abc', '1n']))})",
                "new Array<number>(-1).length",
                "JSON.stringify({ a: 10n })",
                "([] as number[]).reduce((a, b) => a + b)",
                f"new Date(NaN).toISOString()",
                f"(2n ** {self.pick(['-1n'])})",
            ]) + ");")
        elif k == 3:
            fe = [f for f in self.funcs if f[2] == "throws"]
            if fe:
                self.emit(ind + 1, f"console.log({fe[0][0]}({self.num(d - 1)}));")
            else:
                self.emit(ind + 1, f"if ({self.guard(self.boolean(d - 1))}) throw new Error({self.string(d - 1)});")
        elif k == 4:
            self.emit(ind + 1, f"console.log([1, 2, 3].map((q) => {{ if (q === {r.randrange(1, 5)}) throw new TypeError(\"cb \" + q); return q * 2; }}).join());")
        else:
            # nested try with rethrow
            e2 = self.fresh("e")
            self.emit(ind + 1, "try {")
            self.emit(ind + 2, f"if ({self.guard(self.boolean(d - 1))}) throw new RangeError({self.string(d - 1)});")
            self.emit(ind + 2, "console.log(\"inner ok\");")
            self.emit(ind + 1, f"}} catch ({e2}) {{")
            self.emit(ind + 2, f"console.log(\"inner\", {e2} instanceof RangeError);")
            self.emit(ind + 2, f"if ({self.guard(self.boolean(1))}) throw {e2};")
            self.emit(ind + 1, "}")
        self.body(ind + 1, d - 1, r.randrange(0, 2))
        self.pop()
        if self.chance(0.1):
            self.emit(ind, "} catch {")
            self.emit(ind + 1, "console.log(\"caught\");")
        else:
            self.emit(ind, f"}} catch ({e}) {{")
            if lib_err:
                self.emit(ind + 1, f"if ({e} instanceof Error) console.log(\"lib\", {e}.name, {e} instanceof RangeError, {e} instanceof TypeError, {e} instanceof SyntaxError);")
            else:
                self.emit(ind + 1, f"if ({e} instanceof RangeError) console.log(\"range\", {e}.message);")
                self.emit(ind + 1, f"else if ({e} instanceof Error) console.log({e}.name, {e}.message, String({e}), `${{{e}}}`, JSON.stringify({e}));")
                self.emit(ind + 1, f"else console.log(\"other\");")
        self.emit(ind, "}")

    def stmt_destructure(self, ind, d):
        k = self.r.randrange(3)
        if k == 0:
            a, b = self.fresh(), self.fresh()
            src = self.tup(d) if self.chance(0.5) else f"[{self.num(d)}, {self.string(d)}]"
            self.emit(ind, f"const [{a}, {b}] = {src};")
            self.declare(a, NUM, False)
            self.declare(b, STR, False)
        elif k == 1:
            a, b = self.fresh(), self.fresh()
            self.emit(ind, f"let {a}: number = {self.num(d)};")
            self.emit(ind, f"let {b}: number = {self.num(d)};")
            self.emit(ind, f"[{a}, {b}] = [{b}, {a}];")
            self.emit(ind, f"console.log({a}, {b});")
            self.declare(a, NUM, True)
            self.declare(b, NUM, True)
        else:
            rc = self.var(REC)
            if not rc:
                return self.stmt_print(ind, d)
            a, b = self.fresh(), self.fresh()
            self.emit(ind, f"const {{ id: {a}, label: {b} }} = {rc};")
            self.declare(a, NUM, False)
            self.declare(b, STR, False)

    def stmt(self, ind, d):
        k = self.r.randrange(100)
        if d <= 0:
            k = self.r.choice([0, 30, 50])
        if k < 25:
            self.stmt_decl(ind, max(d, 1))
        elif k < 50:
            self.stmt_print(ind, max(d, 1))
        elif k < 66:
            self.stmt_mutate(ind, max(d, 1))
        elif k < 74:
            self.stmt_if(ind, d - 1)
        elif k < 86:
            self.stmt_loop(ind, d - 1)
        elif k < 93:
            self.stmt_try(ind, d - 1)
        else:
            self.stmt_destructure(ind, max(d, 1))

    # ---------------------------------------------------------------- declarations
    def gen_classes(self):
        r = self.r
        n = r.randrange(0, 4)
        for ci in range(n):
            parent = self.pick(self.classes) if self.classes and self.chance(0.7) else None
            c = Cls(f"K{ci}", parent)
            ftypes = [NUM, STR, BOOL, NARR]
            own = []
            for fi in range(r.randrange(1, 3)):
                own.append((f"f{ci}_{fi}", self.pick(ftypes), self.chance(0.3)))
            c.fields = list(own)
            use_pp = self.chance(0.3)  # parameter properties need the TypeScript transform in Node
            lines = []
            hdr = f"class {c.name}" + (f" extends {parent.name}" if parent else "") + " {"
            lines.append(hdr)
            if not parent:
                lines.append("  static count: number = 0;")
                lines.append(f"  label: string = {tslit(self.pick(STR_LITS))};")
                c.fields = [("label", STR, False)] + own
            if self.chance(0.4):
                lines.append(f"  tag{ci}: string = \"{c.name}\";")
                c.fields.append((f"tag{ci}", STR, False))
            for (f, t, ro) in own:
                if not use_pp:
                    lines.append(f"  {'readonly ' if ro else ''}{f}: {t};")
            if self.chance(0.3):
                lines.append(f"  fn{ci} = (): number => this.total() + 1;")
            pparams = parent.ctor_params if parent else []
            base_args = [f"a{i}" for i in range(len(pparams))]
            params = [f"a{i}: {t}" for i, t in enumerate(pparams)]
            for (f, t, ro) in own:
                params.append((f"{'readonly' if ro else 'public'} {f}: {t}") if use_pp else f"{f}: {t}")
            c.ctor_params = list(pparams) + [t for (_, t, _) in own]
            lines.append(f"  constructor({', '.join(params)}) {{")
            if parent:
                lines.append(f"    super({', '.join(base_args)});")
            if not use_pp:
                for (f, t, ro) in own:
                    lines.append(f"    this.{f} = {f};")
            lines.append(f"    {c.name}.count++;" if not parent else f"    {self.pick(c.chain()).name}.count += 10;")
            lines.append("  }")
            # methods: m(x), total(), describe(), kind(), bump(d)
            self.push()
            for (f, t, ro) in c.all_fields():
                self.declare(f"this.{f}", t, False)
            self.declare("x", NUM, False)
            body = self.num(2)
            if parent and self.chance(0.6):
                body = f"super.m(x) + {body}"
            lines.append(f"  m(x: number): number {{ return {body}; }}")
            self.pop()
            self.push()
            for (f, t, ro) in c.all_fields():
                self.declare(f"this.{f}", t, False)
            nums = [f"this.{f}" for (f, t, _) in c.all_fields() if t == NUM]
            tot = " + ".join(nums) if nums else "0"
            if not parent or self.chance(0.5):
                lines.append(f"  total(): number {{ return {tot}{' + super.total()' if parent and self.chance(0.5) else ''}; }}")
            lines.append(f"  kind(): string {{ return \"{c.name}\"; }}")
            if not parent or self.chance(0.5):
                lines.append(f"  describe(): string {{ return `${{this.kind()}}(${{this.total()}}) {self.tmpl_part(1)}`; }}")
            muts = [f for (f, t, ro) in c.all_fields() if t == NUM and not ro]
            bump = "; ".join(f"this.{f} += d" for f in muts) if muts else "this.label += String(d)"
            if not parent or self.chance(0.4):
                lines.append(f"  bump(d: number): void {{ {bump}; }}")
            self.pop()
            if not parent:
                saved, self.scopes = self.scopes, [[]]
                args = ", ".join(self.gen(t, 0) if t != NUM else "n" for t in c.ctor_params)
                self.scopes = saved
                lines.append(f"  static make(n: number): {c.name} {{ return new {c.name}({args}); }}")
            lines.append("}")
            self.lines += lines
            self.classes.append(c)

    def gen_functions(self):
        r = self.r
        # helpers used by expressions
        self.lines += [
            "function ix(i: number, n: number): number {",
            "  const k = Math.trunc(i);",
            "  if (!Number.isFinite(k)) return 0;",
            "  return ((k % n) + n) % n;",
            "}",
            "function cmpNum(a: number, b: number): number {",
            "  if (Number.isNaN(a)) return Number.isNaN(b) ? 0 : 1;",
            "  if (Number.isNaN(b)) return -1;",
            "  return a < b ? -1 : a > b ? 1 : 0;",
            "}",
            "function nz(b: bigint): bigint {",
            "  return b === 0n ? 1n : b;",
            "}",
            "function toBig(n: number): bigint {",
            "  return Number.isFinite(n) ? BigInt(Math.trunc(n % 1e15)) : 0n;",
            "}",
            "function approx(x: number): number {",
            "  return Number(x.toPrecision(12));",
            "}",
            "function maybe(n: number): number | null {",
            "  return n > 0 ? n : null;",
            "}",
            "function maybeStr(s: string): string | undefined {",
            "  return s.length > 2 ? s : undefined;",
            "}",
            "function makeAdder(k: number): (x: number) => number {",
            "  let calls = 0;",
            "  return (x: number) => {",
            "    calls++;",
            "    return x + k * calls;",
            "  };",
            "}",
            "function compose(f: (x: number) => number, g: (x: number) => number): (x: number) => number {",
            "  return (x: number) => g(f(x));",
            "}",
        ]
        for fi in range(r.randrange(1, 5)):
            kind = self.pick(["num", "num", "str", "bool", "rec", "rest", "opt", "thrower"])
            nm = f"{kind if kind != 'num' else 'fn'}{fi}"
            saved = self.scopes
            self.scopes = [[]]
            self.in_fn = True
            if kind == "rec":
                self.lines += [f"function {nm}(n: number): number {{",
                               "  if (n <= 1) return n;",
                               f"  return {nm}(n - 1) + {nm}(n - 2){self.pick(['', ' * 2', ' - 1', ' + n'])};", "}"]
                self.funcs.append((nm, ["rec"], NUM))
            elif kind == "rest":
                self.lines += [f"function {nm}(base: number, ...xs: number[]): number {{",
                               "  let s = base;", "  for (const x of xs) s = s * 2 + x;", "  return s + xs.length;", "}"]
                self.funcs.append((nm, [NUM, "rest"], NUM))
            elif kind == "opt":
                self.declare("a", NUM, False)
                dflt = self.pick([self.num(0), "a", f"a * {self.num(0)}"])
                body = self.num(2)
                self.lines += [f"function {nm}(a: number, b: number = {dflt}, c?: number): number {{",
                               f"  return {body} + b * 2 + (c ?? 0.5);", "}"]
                self.funcs.append((nm, [NUM, "opt", "opt"], NUM))
            elif kind == "thrower":
                self.lines += [f"function {nm}(n: number): number {{",
                               f"  if (n > {self.num(0)}) throw new {self.pick(ERR_TYPES)}(\"too big: \" + n);",
                               "  return n * 10;", "}"]
                self.funcs.append((nm, [NUM], "throws"))
            else:
                ret = {"num": NUM, "str": STR, "bool": BOOL}[kind]
                ps = [(f"p{i}", self.pick([NUM, NUM, STR, BOOL, NARR])) for i in range(r.randrange(1, 4))]
                for (p, t) in ps:
                    self.declare(p, t, True)
                self.lines.append(f"function {nm}({', '.join(f'{p}: {t}' for p, t in ps)}): {ret} {{")
                for _ in range(r.randrange(0, 4)):
                    self.stmt(1, 2)
                self.lines.append(f"  return {self.gen(ret, 2)};")
                self.lines.append("}")
                self.funcs.append((nm, [t for _, t in ps], ret))
            self.in_fn = False
            self.scopes = saved

    def program(self, nstmts):
        self.lines.append("// generated by tests/difffuzz/gen.py")
        self.lines.append("interface Rec { id: number; label: string; score?: number; tags: string[] }")
        self.gen_functions()
        self.gen_classes()
        for _ in range(nstmts):
            self.stmt(0, 3)
        if self.chance(0.03):
            self.lines.append("throw new Error(\"final uncaught\");")
        return "\n".join(self.lines) + "\n"


def generate(seed, index, ints=True, size=None, exact_math=False):
    rng = random.Random(f"e7-difffuzz:{seed}:{index}")
    g = Gen(rng, ints=ints, exact_math=exact_math)
    n = size if size is not None else rng.randrange(10, 40)
    return g.program(n)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--index", type=int, default=0)
    ap.add_argument("--no-ints", action="store_true", help="do not generate fixed-width integers")
    ap.add_argument("--size", type=int, help="number of top-level statements")
    ap.add_argument("--exact-math", action="store_true", help="print results of fdlibm-based Math functions exactly")
    ap.add_argument("-o", "--output")
    a = ap.parse_args()
    src = generate(a.seed, a.index, ints=not a.no_ints, size=a.size, exact_math=a.exact_math)
    if a.output:
        with open(a.output, "w", encoding="utf-8") as f:
            f.write(src)
    else:
        sys.stdout.write(src)


if __name__ == "__main__":
    main()
