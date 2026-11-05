// A module exercising most of the runtime (strings, arrays, maps, closures, classes with static
// fields, try/catch, crypto, JSON, BigInt, a sort comparator, a host native), shared by the soak test
// and the threads test.
#pragma once

static const char* kWorkloadSource = R"E7(
import { sha256, hmac } from "e7:crypto";
import { hexEncode } from "e7:encoding";

declare function hostMix(a: number, b: string): number;

class Account {
  static created = 0;
  static readonly PREFIX: string = "acct-";
  balance = 0;
  history: number[] = [];
  constructor(public id: string) { Account.created++; }
  deposit(n: number): void {
    if (n <= 0) throw new RangeError("bad amount " + n);
    this.balance += n;
    this.history.push(n);
    if (this.history.length > 8) this.history.shift();
  }
}

interface Order { id: number; items: string[]; total: number; }

const cache = new Map<string, string>();
const enc = new TextEncoder();

function makeCounter(start: number): () => number {
  let c = start;
  return () => { c += 1; return c; };
}

export function strings(seed: number): string {
  const parts: string[] = [];
  for (let i = 0; i < 12; i++) parts.push("p" + ((seed + i) % 7) + "-" + i);
  let s = parts.join(",");
  s = s.replace("p1", "P1").toUpperCase().split(",").reverse().join("|");
  return `${s.length}:${s.slice(0, 12)}:${s.indexOf("P")}:${s.padStart(4, "x").substring(2, 6)}`;
}

export function arrays(seed: number): number {
  const a: number[] = [];
  for (let i = 0; i < 40; i++) a.push((seed * 31 + i * 17) % 101);
  const b = a.filter((x) => x % 2 === 0).map((x) => x * 3);
  b.sort((x, y) => y - x);
  return b.reduce((acc, x) => acc + x, 0) + a.indexOf(seed % 101) + b.length;
}

export function maps(seed: number): number {
  const m = new Map<string, number[]>();
  for (let i = 0; i < 30; i++) m.set("k" + ((seed + i) % 20), [i, seed]);
  for (let i = 0; i < 10; i++) m.delete("k" + i);
  const s = new Set<number>();
  let t = 0;
  for (const [k, v] of m) { t += v[0] + k.length; s.add(v[0] % 5); }
  // a bounded cache in a global: state is kept, but it does not grow
  cache.set("c" + (seed % 50), "v" + seed);
  return t + s.size + m.size + cache.size;
}

export function closures(seed: number): number {
  const counters: (() => number)[] = [];
  for (let i = 0; i < 6; i++) counters.push(makeCounter(seed + i));
  let t = 0;
  for (const c of counters) { c(); t += c(); }
  return t;
}

export function errors(seed: number): string {
  const acct = new Account(Account.PREFIX + seed);
  let caught = 0;
  for (let i = -2; i < 6; i++) {
    try {
      acct.deposit(i);
    } catch (e) {
      if (e instanceof RangeError) caught++;
    }
  }
  try {
    const arr = [1, 2, 3];
    if (seed % 3 === 0) throw new Error("boom " + arr.length);
  } catch (e) {
    caught += 10;
  }
  return acct.id + ":" + acct.balance + ":" + acct.history.length + ":" + caught;
}

export function crypto(seed: number): string {
  const data = enc.encode("payload-" + seed);
  const h = hexEncode(sha256(data));
  const m = hexEncode(hmac("sha256", enc.encode("key"), data));
  return h.slice(0, 16) + m.slice(0, 16);
}

export function json(seed: number): string {
  const o: Order = { id: seed, items: ["a" + seed, "b", "c"], total: seed * 1.5 };
  const text = JSON.stringify(o);
  const back = JSON.parse(text) as Order;
  return text.length + ":" + back.items.join("") + ":" + back.total;
}

export function bigints(seed: number): string {
  let x = BigInt(seed) + 1n;
  for (let i = 0; i < 6; i++) x = x * 1000003n + BigInt(i);
  return (x % 1000000007n).toString() + ":" + x.toString(16).length;
}

export function host(seed: number): number {
  return hostMix(seed, "s" + seed);
}

export function mix(seed: number): string {
  return strings(seed) + "/" + arrays(seed) + "/" + maps(seed) + "/" + closures(seed) + "/" + errors(seed) + "/" +
    crypto(seed) + "/" + json(seed) + "/" + bigints(seed) + "/" + host(seed) + "/" + Account.created;
}

export function spin(n: number): number {
  let t = 0;
  for (let i = 0; i < n; i++) t = (t * 31 + i) % 1000003;
  return t;
}
)E7";

static const char* kWorkloadFunctions[] = {"strings", "arrays", "maps", "closures", "errors",
                                           "crypto",  "json",   "bigints", "host",   "mix"};
static const size_t kWorkloadFunctionCount = sizeof kWorkloadFunctions / sizeof *kWorkloadFunctions;

// hostMix(a, b) = a * 7 + b.length
static e7_status workloadHostMix(e7_vm*, const e7_value* args, size_t, e7_value* ret, void*) {
  ret->type = E7_NUMBER;
  ret->v.num = args[0].v.num * 7 + (double)args[1].v.str.len;
  return E7_OK;
}

#if defined(E7_GC_STRESS) && E7_GC_STRESS
static const bool kWorkloadStress = true;
#else
static const bool kWorkloadStress = false;
#endif
