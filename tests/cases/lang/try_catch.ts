// try/catch (0.2): output is compared with Node.js
function risky(n: number): number {
  if (n > 2) throw new RangeError("too big: " + n);
  return n * 10;
}
for (let i = 0; i < 5; i++) {
  try {
    console.log("ok", risky(i));
  } catch (e) {
    if (e instanceof RangeError) console.log("range", e.message, e.name);
    else if (e instanceof Error) console.log("other", String(e));
  }
}

// errors raised by the standard library that Node.js also throws
try {
  const v = JSON.parse("{bad") as number[];
  console.log(v.length);
} catch (e) {
  console.log(e instanceof SyntaxError, e instanceof Error, e instanceof TypeError);
}
try {
  const x: bigint = 1n / 0n;
  console.log(x);
} catch (err) {
  console.log(String(err));
}
try {
  console.log((1.5).toFixed(200));
} catch (e) {
  console.log(e instanceof RangeError);
}
try {
  console.log(decodeURIComponent("%"));
} catch (e) {
  console.log(e instanceof URIError);
}
const empty: number[] = [];
try {
  empty.reduce((a: number, b: number): number => a + b);
} catch (e) {
  console.log(`${e}`);
}

// through native callbacks, and assignments made before the error are kept
let n = 0;
try {
  n = 1;
  [1, 2].map((v: number): number => {
    if (v === 2) throw new TypeError("in callback");
    return v;
  });
  n = 2;
} catch (e) {
  console.log("caught callback", n, e instanceof TypeError ? e.message : "?");
}

// nesting, rethrow, identity, no binding
function nested(): string {
  try {
    try {
      throw new Error("inner");
    } catch (e) {
      throw e;
    }
  } catch (e) {
    return e instanceof Error ? "rethrown " + e.message : "";
  }
}
console.log(nested());
const saved = new Error("kept");
try {
  throw saved;
} catch (e) {
  console.log(e === saved, saved.name, String(saved), saved.toString());
}
try {
  throw new Error();
} catch {
  console.log("no binding", String(new Error()), String(new TypeError("")));
}

// deep recursion unwinds to the handler; loops continue; break and return inside try
function down(k: number): number {
  if (k === 0) throw new Error("bottom");
  return down(k - 1) + 1;
}
let caught = 0;
for (let i = 0; i < 3; i++) {
  try {
    down(50 + i);
  } catch (e) {
    caught++;
  }
}
console.log("caught", caught);
function find(xs: number[], want: number): number {
  for (let i = 0; i < xs.length; i++) {
    try {
      if (xs[i] === want) return i;
      if (xs[i] < 0) break;
    } catch (e) {
      return -2;
    }
  }
  return -1;
}
console.log(find([3, 4, 5], 5), find([3, -1, 5], 5));

// a caught variable captured by a closure
const handlers: (() => string)[] = [];
for (const msg of ["a", "b"]) {
  try {
    throw new Error(msg);
  } catch (e) {
    handlers.push((): string => (e instanceof Error ? e.message : ""));
  }
}
console.log(handlers.map((h: () => string): string => h()).join(","));
