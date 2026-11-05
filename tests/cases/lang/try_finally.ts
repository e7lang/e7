// try / finally and try / catch / finally: the finally block runs on normal completion, return,
// break, continue and catchable errors (rethrown after it); return / throw / break in finally
// override the pending completion.

const log: string[] = [];

function normal(): number {
  let x = 1;
  try {
    x = 2;
  } finally {
    log.push("normal finally " + x);
  }
  return x;
}
console.log(normal(), log.join("; "));

// the return value is computed before the finally block runs, which cannot change it
function retValue(): number {
  let x = 1;
  try {
    return x;
  } finally {
    x = 5;
    log.push("ret finally x=" + x);
  }
}
console.log(retValue());

function retCall(): string {
  const steps: string[] = [];
  const f = (s: string): string => {
    steps.push(s);
    return s;
  };
  try {
    return f("a") + steps.length;
  } finally {
    f("finally");
  }
}
console.log(retCall());

// return in finally overrides return in try
function override(): string {
  try {
    return "try";
  } finally {
    // eslint-disable-next-line no-unsafe-finally
    return "finally";
  }
}
console.log(override());

// return in finally discards a thrown error
function swallow(): string {
  try {
    throw new Error("lost");
  } finally {
    return "swallowed";
  }
}
console.log(swallow());

// throw in finally replaces the pending completion
function replaceThrow(): string {
  try {
    try {
      throw new TypeError("first");
    } finally {
      throw new RangeError("second");
    }
  } catch (e) {
    return String(e);
  }
}
console.log(replaceThrow());

function replaceReturn(): string {
  try {
    try {
      return "value";
    } finally {
      throw new Error("from finally");
    }
  } catch (e) {
    if (e instanceof Error) return "caught " + e.message;
    return "?";
  }
}
console.log(replaceReturn());

// errors are rethrown after the finally block
function rethrow(): string {
  const seen: string[] = [];
  try {
    try {
      seen.push("body");
      JSON.parse("{bad") as number;
      seen.push("not reached");
    } finally {
      seen.push("finally");
    }
  } catch (e) {
    if (e instanceof SyntaxError) seen.push("outer caught " + e.name);
  }
  return seen.join(",");
}
console.log(rethrow());

// try / catch / finally: finally runs after the catch block, and after an error in it
function tcf(fail: boolean, failInCatch: boolean): string {
  const seen: string[] = [];
  try {
    try {
      seen.push("try");
      if (fail) throw new Error("e1");
      seen.push("try end");
    } catch (e) {
      seen.push("catch " + (e instanceof Error ? e.message : "?"));
      if (failInCatch) throw new RangeError("e2");
      seen.push("catch end");
    } finally {
      seen.push("finally");
    }
  } catch (e) {
    seen.push("outer " + String(e));
  }
  return seen.join(",");
}
console.log(tcf(false, false));
console.log(tcf(true, false));
console.log(tcf(true, true));

// return from the catch block
function retFromCatch(): number {
  let n = 0;
  try {
    throw new Error("x");
  } catch {
    n += 10;
    return n;
  } finally {
    n += 100;
    log.push("catch-return finally n=" + n);
  }
}
console.log(retFromCatch());

// loops: break and continue run the finally block
function loops(): string {
  const out: string[] = [];
  for (let i = 0; i < 5; i++) {
    try {
      if (i === 1) continue;
      if (i === 3) break;
      out.push("body" + i);
    } finally {
      out.push("fin" + i);
    }
  }
  let j = 0;
  while (true) {
    try {
      j++;
      if (j > 2) break;
    } finally {
      out.push("w" + j);
    }
  }
  return out.join(" ");
}
console.log(loops());

// break / continue inside finally override the pending completion
function breakInFinally(): string {
  const out: string[] = [];
  for (const k of [1, 2, 3]) {
    try {
      if (k === 2) return "returned " + k;
      out.push("k" + k);
    } finally {
      if (k === 2) {
        out.push("break at " + k);
        // eslint-disable-next-line no-unsafe-finally
        break;
      }
    }
  }
  return out.join(" ");
}
console.log(breakInFinally());

function continueInFinally(): string {
  const out: string[] = [];
  for (let k = 0; k < 3; k++) {
    try {
      if (k === 1) throw new Error("dropped " + k);
      out.push("k" + k);
    } finally {
      // eslint-disable-next-line no-unsafe-finally
      continue;
    }
  }
  return out.join(" ");
}
console.log(continueInFinally());

// nested finally blocks run innermost first; labeled break across several
function nested(): string {
  const out: string[] = [];
  outer: for (let i = 0; i < 3; i++) {
    try {
      for (let j = 0; j < 3; j++) {
        try {
          try {
            if (j === 1) continue;
            if (i === 1 && j === 2) break outer;
            out.push(`${i}${j}`);
          } finally {
            out.push("a");
          }
        } finally {
          out.push("b");
        }
      }
    } finally {
      out.push("c" + i);
    }
  }
  return out.join(" ");
}
console.log(nested());

// a return leaving several finally blocks, with a try / catch in between
function deepReturn(): number {
  let total = 0;
  try {
    try {
      try {
        return 1;
      } catch {
        return -1000;
      } finally {
        total += 1;
        log.push("deep inner " + total);
      }
    } finally {
      total += 10;
      log.push("deep middle " + total);
    }
  } finally {
    total += 100;
    log.push("deep outer " + total);
  }
}
console.log(deepReturn());

// an error thrown by a finally block that runs for a return is not caught by try / catch
// statements inside the try block
function finallyErrorScope(): string {
  try {
    try {
      try {
        return "unreachable";
      } catch {
        return "wrong: inner catch";
      }
    } finally {
      throw new Error("finally error");
    }
  } catch (e) {
    return "outer catch: " + (e instanceof Error ? e.message : "");
  }
}
console.log(finallyErrorScope());

// try / catch inside a finally block
function catchInFinally(): string {
  const out: string[] = [];
  try {
    out.push("body");
  } finally {
    try {
      throw new Error("inside");
    } catch (e) {
      out.push("caught " + (e instanceof Error ? e.message : ""));
    } finally {
      out.push("inner finally");
    }
  }
  return out.join(",");
}
console.log(catchInFinally());

// closures created in the finally block (one per way of leaving)
function closures(): number[] {
  const fs: (() => number)[] = [];
  for (let i = 0; i < 3; i++) {
    try {
      if (i === 1) continue;
    } finally {
      const k = i * 10;
      fs.push(() => k + i);
    }
  }
  return fs.map((f) => f());
}
console.log(closures());

// errors raised by library functions run the finally block too
function libError(): string {
  const out: string[] = [];
  try {
    try {
      out.push("x".repeat(-1));
    } finally {
      out.push("finally");
    }
  } catch (e) {
    out.push(e instanceof RangeError ? e.name : "?");
  }
  return out.join(" | ");
}
console.log(libError());

// switch inside try / finally
function sw(k: number): string {
  let s = "";
  try {
    switch (k) {
      case 1:
        s += "one";
        break;
      case 2:
        return "two";
      default:
        s += "other";
    }
    s += ";";
  } finally {
    s += "!";
    log.push("sw " + k + " " + s);
  }
  return s;
}
console.log(sw(1), sw(2), sw(3));

// methods and arrow functions
class Res {
  open = 0;
  use(fail: boolean): string {
    this.open++;
    try {
      if (fail) throw new Error("use failed");
      return "used " + this.open;
    } finally {
      this.open--;
    }
  }
}
const r = new Res();
console.log(r.use(false), r.open);
try {
  r.use(true);
} catch (e) {
  console.log(String(e), r.open);
}
const arrow = (n: number): number => {
  try {
    return n * 2;
  } finally {
    log.push("arrow " + n);
  }
};
console.log(arrow(21));

// top-level try / finally
let topCount = 0;
try {
  topCount++;
} finally {
  topCount += 10;
}
console.log("top", topCount);

console.log(log.join("\n"));

// a finally block in a callback called by a library function (the error leaves the native call)
const cmpLog: string[] = [];
try {
  [3, 1, 2].sort((x, y) => {
    try {
      if (x === 1 || y === 1) throw new Error("cmp");
      return x - y;
    } finally {
      cmpLog.push("f");
    }
  });
} catch (e) {
  cmpLog.push("caught " + (e instanceof Error ? e.message : ""));
}
console.log(cmpLog[cmpLog.length - 1], cmpLog.length > 1);

// labeled block and do...while
const lb: string[] = [];
blk: {
  try {
    break blk;
  } finally {
    lb.push("labeled finally");
  }
}
let k = 0;
do {
  try {
    k++;
    if (k % 2 === 0) continue;
    lb.push("k" + k);
  } finally {
    lb.push("f" + k);
  }
} while (k < 4);
console.log(lb.join(" "));
