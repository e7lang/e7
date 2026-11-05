// C API integration tests.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "e7/e7.h"

static int failures = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                     \
    }                                                                 \
  } while (0)

static std::vector<uint8_t> compile(const char* src) {
  uint8_t* out = nullptr;
  size_t len = 0;
  char* errs = nullptr;
  e7_status st = e7_compile(src, strlen(src), "test.ts", &out, &len, &errs);
  if (st != E7_OK) {
    fprintf(stderr, "compile failed: %s\n", errs ? errs : "?");
    e7_free(errs);
    failures++;
    return {};
  }
  std::vector<uint8_t> v(out, out + len);
  e7_free(out);
  return v;
}

static e7_value num(double d) { e7_value v; v.type = E7_NUMBER; v.v.num = d; return v; }
static e7_value str(const char* s) { e7_value v; v.type = E7_STRING; v.v.str.ptr = s; v.v.str.len = strlen(s); return v; }
static e7_value bytes(const uint8_t* p, size_t n) { e7_value v; v.type = E7_BYTES; v.v.bytes.ptr = p; v.v.bytes.len = n; return v; }

static std::string printed;
static void capturePrint(void*, const char* s, size_t n) { printed.append(s, n); printed += "\n"; }

static e7_status hostAdd(e7_vm*, const e7_value* a, size_t n, e7_value* ret, void* user) {
  (void)n;
  ret->type = E7_NUMBER;
  ret->v.num = a[0].v.num + a[1].v.num + (user ? *(double*)user : 0);
  return E7_OK;
}
static std::string hostBuf;
static e7_status hostUpper(e7_vm*, const e7_value* a, size_t, e7_value* ret, void*) {
  hostBuf.assign(a[0].v.str.ptr, a[0].v.str.len);
  for (auto& c : hostBuf) c = (char)toupper((unsigned char)c);
  ret->type = E7_STRING;
  ret->v.str.ptr = hostBuf.c_str();
  ret->v.str.len = hostBuf.size();
  return E7_OK;
}
static e7_status hostFail(e7_vm* vm, const e7_value*, size_t, e7_value*, void*) {
  e7_native_error(vm, "host says no");
  return E7_ERR_RUNTIME;
}
static const uint8_t kSecret[] = "super-secret-hmac-key";
static int keyProvider(void*, int64_t id, const uint8_t** key, size_t* len) {
  if (id != 7) return -1;
  *key = kSecret;
  *len = sizeof kSecret - 1;
  return 0;
}

int main() {
  // ---- basic call / marshaling
  {
    e7_vm* vm = e7_vm_new(nullptr);
    e7_set_print(vm, capturePrint, nullptr);
    auto bc = compile(R"(
      export function add(a: number, b: number): number { return a + b; }
      export function greet(name: string): string { return `hi ${name}`; }
      export function rev(b: Uint8Array): Uint8Array { return b.slice().reverse(); }
      export function flag(x: boolean): boolean { return !x; }
      export function wrap(x: u32): u32 { return x + 1; }
      export function neg(x: i64): i64 { return -x; }
      export function nothing(): void { console.log("side effect"); }
      export function fails(): number { throw new Error("boom"); }
      let counter = 0;
      export function inc(): number { return ++counter; }
    )");
    e7_module* m = nullptr;
    CHECK(e7_load(vm, bc.data(), bc.size(), &m) == E7_OK);
    e7_value args[2] = {num(2), num(40)}, ret;
    CHECK(e7_call(vm, m, "add", args, 2, &ret) == E7_OK && ret.type == E7_NUMBER && ret.v.num == 42);
    e7_value s = str("Ann");
    CHECK(e7_call(vm, m, "greet", &s, 1, &ret) == E7_OK && ret.type == E7_STRING && std::string(ret.v.str.ptr, ret.v.str.len) == "hi Ann");
    uint8_t raw[3] = {1, 2, 3};
    e7_value b = bytes(raw, 3);
    CHECK(e7_call(vm, m, "rev", &b, 1, &ret) == E7_OK && ret.type == E7_BYTES && ret.v.bytes.len == 3 && ret.v.bytes.ptr[0] == 3);
    e7_value f; f.type = E7_BOOL; f.v.boolean = 0;
    CHECK(e7_call(vm, m, "flag", &f, 1, &ret) == E7_OK && ret.type == E7_BOOL && ret.v.boolean == 1);
    e7_value u; u.type = E7_UINT; u.v.u = 0xffffffffu;
    CHECK(e7_call(vm, m, "wrap", &u, 1, &ret) == E7_OK && ret.type == E7_UINT && ret.v.u == 0);
    e7_value i; i.type = E7_INT; i.v.i = 5;
    CHECK(e7_call(vm, m, "neg", &i, 1, &ret) == E7_OK && ret.type == E7_INT && ret.v.i == -5);
    printed.clear();
    CHECK(e7_call(vm, m, "nothing", nullptr, 0, &ret) == E7_OK && ret.type == E7_VOID && printed == "side effect\n");
    CHECK(e7_call(vm, m, "fails", nullptr, 0, &ret) == E7_ERR_RUNTIME && strstr(e7_last_error(vm), "boom"));
    // VM still usable after an error; state persists between calls
    CHECK(e7_call(vm, m, "inc", nullptr, 0, &ret) == E7_OK && ret.v.num == 1);
    CHECK(e7_call(vm, m, "inc", nullptr, 0, &ret) == E7_OK && ret.v.num == 2);
    CHECK(e7_call(vm, m, "missing", nullptr, 0, &ret) == E7_ERR_NOT_FOUND);
    CHECK(e7_call(vm, m, "add", args, 1, &ret) == E7_ERR_ARGS);
    CHECK(e7_call(vm, m, "add", &s, 2, &ret) == E7_ERR_ARGS);
    CHECK(e7_has_function(m, "add") && !e7_has_function(m, "nope"));
    e7_vm_free(vm);
  }
  // ---- host natives
  {
    e7_vm* vm = e7_vm_new(nullptr);
    double bonus = 0.5;
    CHECK(e7_register_native(vm, "hostAdd", "nn>n", hostAdd, &bonus) == E7_OK);
    CHECK(e7_register_native(vm, "hostUpper", "s>s", hostUpper, nullptr) == E7_OK);
    CHECK(e7_register_native(vm, "hostFail", ">v", hostFail, nullptr) == E7_OK);
    CHECK(e7_register_native(vm, "bad", "q>v", hostFail, nullptr) == E7_ERR_ARGS);
    auto bc = compile(R"(
      declare function hostAdd(a: number, b: number): number;
      declare function hostUpper(s: string): string;
      declare function hostFail(): void;
      export function useHost(): string { return hostUpper("abc") + hostAdd(1, 2); }
      export function callFail(): void { hostFail(); }
    )");
    e7_module* m = nullptr;
    CHECK(e7_load(vm, bc.data(), bc.size(), &m) == E7_OK);
    e7_value ret;
    CHECK(e7_call(vm, m, "useHost", nullptr, 0, &ret) == E7_OK && std::string(ret.v.str.ptr, ret.v.str.len) == "ABC3.5");
    CHECK(e7_call(vm, m, "callFail", nullptr, 0, &ret) == E7_ERR_RUNTIME && strstr(e7_last_error(vm), "host says no"));
    e7_vm_free(vm);
    // unknown native at load time
    e7_vm* vm2 = e7_vm_new(nullptr);
    CHECK(e7_load(vm2, bc.data(), bc.size(), &m) == E7_ERR_LOAD);
    e7_vm_free(vm2);
    // signature mismatch at load time
    e7_vm* vm3 = e7_vm_new(nullptr);
    e7_register_native(vm3, "hostAdd", "n>n", hostAdd, nullptr);
    e7_register_native(vm3, "hostUpper", "s>s", hostUpper, nullptr);
    e7_register_native(vm3, "hostFail", ">v", hostFail, nullptr);
    CHECK(e7_load(vm3, bc.data(), bc.size(), &m) == E7_ERR_LOAD && strstr(e7_last_error(vm3), "signature"));
    e7_vm_free(vm3);
  }
  // ---- key handles: the secret never enters script memory
  {
    e7_vm* vm = e7_vm_new(nullptr);
    e7_set_key_provider(vm, keyProvider, nullptr);
    auto bc = compile(R"(
      import { hmac } from "e7:crypto";
      import { hexEncode } from "e7:encoding";
      import type { KeyHandle } from "e7:crypto";
      export function sign(key: KeyHandle, data: string): string {
        return hexEncode(hmac("sha256", key, new TextEncoder().encode(data)));
      }
    )");
    e7_module* m = nullptr;
    CHECK(e7_load(vm, bc.data(), bc.size(), &m) == E7_OK);
    e7_value args[2];
    args[0].type = E7_HANDLE;
    args[0].v.handle = 7;
    args[1] = str("payload");
    e7_value ret;
    CHECK(e7_call(vm, m, "sign", args, 2, &ret) == E7_OK && ret.type == E7_STRING && ret.v.str.len == 64);
    std::string mac(ret.v.str.ptr, ret.v.str.len);
    // compare with a direct computation using a byte key
    auto bc2 = compile(R"(
      import { hmac } from "e7:crypto";
      import { hexEncode } from "e7:encoding";
      export function sign2(data: string): string {
        const enc = new TextEncoder();
        return hexEncode(hmac("sha256", enc.encode("super-secret-hmac-key"), enc.encode(data)));
      }
    )");
    e7_module* m2 = nullptr;
    CHECK(e7_load(vm, bc2.data(), bc2.size(), &m2) == E7_OK);
    e7_value a1 = str("payload");
    CHECK(e7_call(vm, m2, "sign2", &a1, 1, &ret) == E7_OK && mac == std::string(ret.v.str.ptr, ret.v.str.len));
    args[0].v.handle = 8;
    CHECK(e7_call(vm, m, "sign", args, 2, &ret) == E7_ERR_RUNTIME);
    e7_vm_free(vm);
  }
  // ---- signatures
  {
    auto bc = compile("export function f(): number { return 1; }");
    uint8_t seed[32], pk[32], other[32], otherPk[32];
    for (int i = 0; i < 32; i++) { seed[i] = (uint8_t)i; other[i] = (uint8_t)(i * 7 + 1); }
    e7_public_key(seed, pk);
    e7_public_key(other, otherPk);
    uint8_t* signedBc = nullptr;
    size_t signedLen = 0;
    CHECK(e7_sign_module(bc.data(), bc.size(), seed, &signedBc, &signedLen) == E7_OK);
    e7_config cfg;
    e7_config_init(&cfg);
    cfg.require_signature = 1;
    cfg.trusted_keys = pk;
    cfg.num_trusted_keys = 1;
    e7_vm* vm = e7_vm_new(&cfg);
    e7_module* m;
    CHECK(e7_load(vm, signedBc, signedLen, &m) == E7_OK);
    CHECK(e7_load(vm, bc.data(), bc.size(), &m) == E7_ERR_SIGNATURE);  // unsigned rejected
    std::vector<uint8_t> tampered(signedBc, signedBc + signedLen);
    tampered[20] ^= 1;
    CHECK(e7_load(vm, tampered.data(), tampered.size(), &m) == E7_ERR_SIGNATURE);
    e7_vm_free(vm);
    cfg.trusted_keys = otherPk;  // signed by an untrusted key
    vm = e7_vm_new(&cfg);
    CHECK(e7_load(vm, signedBc, signedLen, &m) == E7_ERR_SIGNATURE);
    e7_vm_free(vm);
    // signing is opt-in: with no trusted keys and no require_signature, unsigned and signed modules
    // both load (the signature trailer is ignored)
    e7_config_init(&cfg);
    vm = e7_vm_new(&cfg);
    CHECK(e7_load(vm, bc.data(), bc.size(), &m) == E7_OK);
    CHECK(e7_load(vm, signedBc, signedLen, &m) == E7_OK);
    CHECK(e7_load(vm, tampered.data(), tampered.size(), &m) == E7_ERR_LOAD);  // CRC still applies
    e7_vm_free(vm);
    // trusted keys without require_signature: unsigned accepted, signatures must verify if present
    e7_config_init(&cfg);
    cfg.trusted_keys = pk;
    cfg.num_trusted_keys = 1;
    vm = e7_vm_new(&cfg);
    CHECK(e7_load(vm, bc.data(), bc.size(), &m) == E7_OK);
    CHECK(e7_load(vm, signedBc, signedLen, &m) == E7_OK);
    e7_vm_free(vm);
    cfg.trusted_keys = otherPk;
    vm = e7_vm_new(&cfg);
    CHECK(e7_load(vm, signedBc, signedLen, &m) == E7_ERR_SIGNATURE);
    e7_vm_free(vm);
    e7_free(signedBc);
  }
  // ---- corrupted / malformed bytecode is rejected, never crashes
  {
    auto bc = compile("export function f(x: number): number { let s = 0; for (let i = 0; i < x; i++) s += i; return s; }");
    e7_vm* vm = e7_vm_new(nullptr);
    e7_module* m;
    std::vector<uint8_t> t = bc;
    t[t.size() / 2] ^= 0x55;
    CHECK(e7_load(vm, t.data(), t.size(), &m) == E7_ERR_LOAD);  // checksum
    CHECK(e7_load(vm, bc.data(), 10, &m) == E7_ERR_LOAD);
    CHECK(e7_load(vm, nullptr, 0, &m) == E7_ERR_LOAD);
    e7_vm_free(vm);
  }
  // ---- budget: hard limit and suspension / resume
  {
    auto bc = compile(R"(
      export function spin(n: number): number { let s = 0; for (let i = 0; i < n; i++) s += i % 7; return s; }
    )");
    e7_vm* vm = e7_vm_new(nullptr);
    e7_module* m;
    CHECK(e7_load(vm, bc.data(), bc.size(), &m) == E7_OK);
    e7_value a = num(1e7), ret;
    e7_set_budget(vm, 1000);
    CHECK(e7_call(vm, m, "spin", &a, 1, &ret) == E7_ERR_BUDGET);
    e7_set_budget(vm, 0);
    a = num(1000);
    CHECK(e7_call(vm, m, "spin", &a, 1, &ret) == E7_OK && ret.v.num == 2997);
    e7_vm_free(vm);
    e7_config cfg;
    e7_config_init(&cfg);
    cfg.suspend_on_budget = 1;
    vm = e7_vm_new(&cfg);
    CHECK(e7_load(vm, bc.data(), bc.size(), &m) == E7_OK);
    a = num(100000);
    e7_set_budget(vm, 5000);
    e7_status st = e7_call(vm, m, "spin", &a, 1, &ret);
    int slices = 1;
    while (st == E7_SUSPENDED) {
      CHECK(e7_call(vm, m, "spin", &a, 1, &ret) == E7_ERR_STATE);  // busy while suspended
      e7_set_budget(vm, 5000);
      st = e7_resume(vm, &ret);
      slices++;
    }
    CHECK(st == E7_OK && slices > 10);
    double want = 0;
    for (int i = 0; i < 100000; i++) want += i % 7;
    CHECK(ret.v.num == want);
    // abandon a suspended call
    e7_set_budget(vm, 100);
    CHECK(e7_call(vm, m, "spin", &a, 1, &ret) == E7_SUSPENDED);
    e7_abort_suspended(vm);
    e7_set_budget(vm, 0);
    a = num(10);
    CHECK(e7_call(vm, m, "spin", &a, 1, &ret) == E7_OK && ret.v.num == 24);
    e7_vm_free(vm);
  }
  // ---- memory limit
  {
    e7_config cfg;
    e7_config_init(&cfg);
    cfg.memory_limit = 4u << 20;
    e7_vm* vm = e7_vm_new(&cfg);
    auto bc = compile(R"(
      export function hog(): number { const keep: string[] = []; for (let i = 0; i < 1e7; i++) keep.push("x".repeat(100) + i); return keep.length; }
      export function churn(): number { let n = 0; for (let i = 0; i < 200000; i++) { const s = "abc" + i; n += s.length; } return n; }
    )");
    e7_module* m;
    CHECK(e7_load(vm, bc.data(), bc.size(), &m) == E7_OK);
    e7_value ret;
    CHECK(e7_call(vm, m, "hog", nullptr, 0, &ret) == E7_ERR_MEMORY);
    // garbage is collected: lots of temporary allocation fits in a small heap
    CHECK(e7_call(vm, m, "churn", nullptr, 0, &ret) == E7_OK && ret.v.num > 0);
    CHECK(e7_memory_used(vm) < (4u << 20));
    e7_vm_free(vm);
  }
  // ---- e7:json tryParse: budget and memory exhaustion stay errors (never a null result)
  {
    auto bc = compile(R"(
      import { tryParse, lastError } from "e7:json";
      export function count(t: string): number { const r = tryParse(t) as number[] | null; return r === null ? -1 : r.length; }
      export function err(): string { return lastError(); }
      export function countAll(t: string): number {
        let n = 0;
        for (let k = 0; k < 3; k++) { const r = tryParse(t) as number[] | null; n += r === null ? -1 : r.length; }
        return n;
      }
    )");
    std::string doc = "[";
    for (int i = 0; i < 300000; i++) doc += i ? ",0" : "0";
    std::string garbage = doc + "]x";
    doc += "]";
    e7_vm* vm = e7_vm_new(nullptr);
    e7_module* m;
    CHECK(e7_load(vm, bc.data(), bc.size(), &m) == E7_OK);
    e7_value a = str(doc.c_str()), ret;
    CHECK(e7_call(vm, m, "count", &a, 1, &ret) == E7_OK && ret.v.num == 300000);
    a = str(garbage.c_str());
    CHECK(e7_call(vm, m, "count", &a, 1, &ret) == E7_OK && ret.v.num == -1);
    CHECK(e7_call(vm, m, "err", nullptr, 0, &ret) == E7_OK && ret.type == E7_STRING &&
          std::string(ret.v.str.ptr, ret.v.str.len).find("unexpected data after JSON value") != std::string::npos);
    // the parse is charged by the size of the text (after the native returns: the next budget
    // check stops the script, whether the parse succeeded or gave null)
    e7_set_budget(vm, 20000);
    a = str(doc.c_str());
    CHECK(e7_call(vm, m, "countAll", &a, 1, &ret) == E7_ERR_BUDGET);
    e7_set_budget(vm, 20000);
    a = str(garbage.c_str());
    CHECK(e7_call(vm, m, "countAll", &a, 1, &ret) == E7_ERR_BUDGET);
    e7_set_budget(vm, 0);
    CHECK(e7_call(vm, m, "countAll", &a, 1, &ret) == E7_OK && ret.v.num == -3);
    e7_set_budget(vm, 0);
    e7_vm_free(vm);
    e7_config cfg;
    e7_config_init(&cfg);
    cfg.memory_limit = 4u << 20;
    vm = e7_vm_new(&cfg);
    CHECK(e7_load(vm, bc.data(), bc.size(), &m) == E7_OK);
    a = str(doc.c_str());
    CHECK(e7_call(vm, m, "count", &a, 1, &ret) == E7_ERR_MEMORY);
    a = str(garbage.c_str());
    CHECK(e7_call(vm, m, "count", &a, 1, &ret) == E7_ERR_MEMORY);
    a = str("[1,2]");
    CHECK(e7_call(vm, m, "count", &a, 1, &ret) == E7_OK && ret.v.num == 2);
    e7_vm_free(vm);
  }
  // ---- stack depth
  {
    e7_vm* vm = e7_vm_new(nullptr);
    auto bc = compile("export function deep(n: number): number { return n <= 0 ? 0 : 1 + deep(n - 1); }");
    e7_module* m;
    CHECK(e7_load(vm, bc.data(), bc.size(), &m) == E7_OK);
    e7_value a = num(5000), ret;
    CHECK(e7_call(vm, m, "deep", &a, 1, &ret) == E7_OK && ret.v.num == 5000);
    a = num(1e7);
    CHECK(e7_call(vm, m, "deep", &a, 1, &ret) == E7_ERR_STACK);
    a = num(10);
    CHECK(e7_call(vm, m, "deep", &a, 1, &ret) == E7_OK && ret.v.num == 10);
    e7_vm_free(vm);
  }
  // ---- compile errors through the API
  {
    uint8_t* out;
    size_t len;
    char* errs = nullptr;
    const char* bad = "const x: number = 'a';";
    CHECK(e7_compile(bad, strlen(bad), "bad.ts", &out, &len, &errs) == E7_ERR_COMPILE && errs && strstr(errs, "bad.ts:1"));
    e7_free(errs);
    e7_vm* vm = e7_vm_new(nullptr);
    e7_module* m;
    CHECK(e7_load_source(vm, bad, strlen(bad), "bad.ts", &m) == E7_ERR_COMPILE);
    const char* good = "export const v = 1; export function f(): number { return v * 3; }";
    CHECK(e7_load_source(vm, good, strlen(good), "good.ts", &m) == E7_OK);
    e7_value ret;
    CHECK(e7_call(vm, m, "f", nullptr, 0, &ret) == E7_OK && ret.v.num == 3);
    e7_vm_free(vm);
  }
  // ---- deterministic Math.random with a seed
  {
    const char* src = "export function r(): number { return Math.random(); }";
    double first[2];
    for (int k = 0; k < 2; k++) {
      e7_vm* vm = e7_vm_new(nullptr);
      e7_seed_random(vm, 42);
      e7_module* m;
      e7_load_source(vm, src, strlen(src), "r.ts", &m);
      e7_value ret;
      e7_call(vm, m, "r", nullptr, 0, &ret);
      first[k] = ret.v.num;
      CHECK(ret.v.num >= 0 && ret.v.num < 1);
      e7_vm_free(vm);
    }
    CHECK(first[0] == first[1]);
  }
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("capi_test: all checks passed\n");
  return 0;
}
