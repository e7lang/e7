// Map / Set natives: insertion-ordered hash tables (JavaScript semantics, SameValueZero keys).
#include "util.h"

namespace e7 {

static uint32_t mix64(uint64_t x) {
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdull;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ull;
  x ^= x >> 33;
  return (uint32_t)x;
}

static uint32_t hashDyn(VM& vm, Slot k, KeyMode km) {
  switch (km) {
    case KM_NUM: {
      double d = k.f;
      if (d == 0) d = 0;  // -0 == +0
      if (d != d) return 0x7FF80000u;
      uint64_t b;
      memcpy(&b, &d, 8);
      return mix64(b);
    }
    case KM_INT: return mix64(k.u);
    default: break;
  }
  Obj* o = k.r;
  if (!o) return 1;
  if (o->type == OT::String) return strHash(static_cast<StrObj*>(o));
  if (o->type == OT::BigInt) {
    auto* b = static_cast<BigObj*>(o);
    uint64_t h = b->neg ? 0x9E37 : 0x7F4A;
    for (uint32_t l : b->mag) h = h * 1099511628211ull + l;
    return mix64(h);
  }
  if (o->type == OT::Box && !(o->flags & 1)) {  // boxed primitives by value; a Date (flag 1) is an object
    auto* bx = static_cast<BoxObj*>(o);
    return hashDyn(vm, bx->v, bx->vt == VT_NUMBER ? KM_NUM : KM_INT);
  }
  return mix64((uint64_t)(uintptr_t)o);
}

static bool keyEq(VM& vm, Slot a, Slot b, KeyMode km) {
  switch (km) {
    case KM_NUM: return a.f == b.f || (a.f != a.f && b.f != b.f);
    case KM_INT: return a.i == b.i;
    default: break;
  }
  Obj* x = a.r;
  Obj* y = b.r;
  if (x == y) return true;
  if (!x || !y) return false;
  if ((x->flags & 1) || (y->flags & 1)) {  // Dates are objects: compared by identity, as in JavaScript
    if (x->type == OT::Box || y->type == OT::Box) return false;
  }
  if (x->type == OT::Box && y->type == OT::Box) {
    auto* bx = static_cast<BoxObj*>(x);
    auto* by = static_cast<BoxObj*>(y);
    if (bx->vt == VT_NUMBER && by->vt == VT_NUMBER) return keyEq(vm, bx->v, by->v, KM_NUM);
    return bx->vt != VT_NUMBER && by->vt != VT_NUMBER && bx->v.i == by->v.i;
  }
  return strictEquals(vm, a, b, VT_REF);
}

static size_t mapBytes(MapObj* m) { return m->entries.capacity() * sizeof(MapEntry) + m->buckets.capacity() * 4; }

int32_t mapFind(VM& vm, MapObj* m, Slot k) {
  if (m->buckets.empty()) return -1;
  uint32_t h = hashDyn(vm, k, m->km);
  int32_t idx = m->buckets[h & (m->buckets.size() - 1)];
  while (idx >= 0) {
    MapEntry& e = m->entries[idx];
    if (e.live && e.hash == h && keyEq(vm, e.k, k, m->km)) return idx;
    idx = e.next;
  }
  return -1;
}

// Accounts a change of the map's storage that has already happened. If that exceeds the memory limit,
// the storage is still recorded before the error propagates: the map keeps it, and freeing it later
// subtracts it again.
static void accountMap(VM& vm, size_t before, size_t after) {
  if (after == before) return;
  int64_t delta = (int64_t)after - (int64_t)before;
  try {
    vm.accountExternal(delta);
  } catch (...) {
    vm.accountExternalUnchecked(delta);
    throw;
  }
}

static void rehash(VM& vm, MapObj* m) {
  size_t before = mapBytes(m);
  // compact dead entries
  if (m->entries.size() != m->count) {
    std::vector<MapEntry> live;
    live.reserve(m->count + 1);
    for (auto& e : m->entries)
      if (e.live) live.push_back(e);
    m->entries.swap(live);
  }
  size_t nb = 8;
  while (nb < (size_t)m->count * 2) nb *= 2;
  m->buckets.assign(nb, -1);
  for (size_t i = 0; i < m->entries.size(); i++) {
    MapEntry& e = m->entries[i];
    uint32_t b = e.hash & (uint32_t)(nb - 1);
    e.next = m->buckets[b];
    m->buckets[b] = (int32_t)i;
  }
  accountMap(vm, before, mapBytes(m));
}

static void mapSet(VM& vm, MapObj* m, Slot k, Slot v) {
  if (m->km == KM_NUM && k.f == 0) k.f = 0.0;  // Map.set / Set.add store a -0 key as +0 (as in JS)
  int32_t idx = mapFind(vm, m, k);
  if (idx >= 0) { m->entries[idx].v = v; return; }
  if (m->count >= 0x7FFFFFF) vm.error("RangeError: Map maximum size exceeded");
  if (m->entries.size() + 1 > m->buckets.size()) rehash(vm, m);
  if (m->km == KM_NUM && k.f == 0) k.f = 0;  // a -0 key is stored as +0 (JavaScript Map.set / Set.add)
  size_t before = mapBytes(m);
  uint32_t h = hashDyn(vm, k, m->km);
  MapEntry e;
  e.k = k;
  e.v = v;
  e.hash = h;
  e.live = true;
  e.seq = m->nextSeq++;
  uint32_t b = h & (uint32_t)(m->buckets.size() - 1);
  e.next = m->buckets[b];
  m->entries.push_back(e);
  m->buckets[b] = (int32_t)(m->entries.size() - 1);
  m->count++;
  accountMap(vm, before, mapBytes(m));
}

void jsonMapSet(VM& vm, MapObj* m, Slot k, Slot v) { mapSet(vm, m, k, v); }

static bool mapDelete(VM& vm, MapObj* m, Slot k) {
  int32_t idx = mapFind(vm, m, k);
  if (idx < 0) return false;
  MapEntry& e = m->entries[idx];
  uint32_t b = e.hash & (uint32_t)(m->buckets.size() - 1);
  int32_t* link = &m->buckets[b];
  while (*link >= 0 && *link != idx) link = &m->entries[*link].next;
  if (*link == idx) *link = e.next;
  e.live = false;
  e.k.u = 0;
  e.v.u = 0;
  m->count--;
  if (m->count == 0) {
    size_t before = mapBytes(m);
    m->entries.clear();
    m->buckets.assign(m->buckets.size(), -1);
    accountMap(vm, before, mapBytes(m));
  }
  return true;
}

static MapObj* newMapChecked(VM& vm, double kvt, double vvt, double km, bool isSet) {
  if (!(kvt >= 0 && kvt < VT__COUNT) || !(vvt >= 0 && vvt < VT__COUNT) || !(km >= 0 && km <= KM_REF))
    vm.error("invalid Map parameters");
  // the key mode decides how key slots are hashed/compared, so it must agree with the key kind
  Kind kk = kindOfVT((uint8_t)kvt);
  KeyMode m = (KeyMode)(int)km;
  if ((kk == K_F) != (m == KM_NUM) || (kk == K_I) != (m == KM_INT)) vm.error("invalid Map parameters");
  return vm.newMap((uint8_t)kvt, (uint8_t)vvt, (KeyMode)(int)km, isSet);
}

E7_NATIVE(m_new) { E7_UNUSED; r->r = newMapChecked(vm, a[0].f, a[1].f, a[2].f, false); }
E7_NATIVE(m_fromEntries) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  MapObj* m = newMapChecked(vm, a[1].f, a[2].f, a[3].f, false);
  if (arr->vt != VT_REF) vm.error("Map entry is not a [key, value] pair");
  for (uint32_t i = 0; i < arr->len; i++) {
    Obj* o = arr->data[i].r;
    if (!o || o->type != OT::Instance) vm.error("Map entry is not a [key, value] pair");
    auto* in = static_cast<InstObj*>(o);
    if (in->cls->nfields < 2 || kindOfVT(in->cls->fieldVT[0]) != kindOfVT(m->kvt) ||
        kindOfVT(in->cls->fieldVT[1]) != kindOfVT(m->vvt))
      vm.error("Map entry has the wrong shape");
    mapSet(vm, m, in->fields()[0], in->fields()[1]);
  }
  r->r = m;
}
E7_NATIVE(s_new) { E7_UNUSED; r->r = newMapChecked(vm, a[0].f, VT_NUMBER, a[1].f, true); }
E7_NATIVE(s_fromArray) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  MapObj* m = newMapChecked(vm, a[1].f, VT_NUMBER, a[2].f, true);
  if (kindOfVT(arr->vt) != kindOfVT(m->kvt)) vm.error("Set element type mismatch");
  Slot z;
  z.u = 0;
  for (uint32_t i = 0; i < arr->len; i++) mapSet(vm, m, arr->data[i], z);
  r->r = m;
}

template <Kind K>
static void checkK(VM& vm, MapObj* m) {
  if (kindOfVT(m->kvt) != K) vm.error("map key type mismatch");
}
template <Kind K>
static void m_get(VM& vm, Slot* a, uint32_t, Slot* r, void*) {
  MapObj* m = Mo(vm, a[0]);
  checkK<K>(vm, m);
  int32_t idx = mapFind(vm, m, a[1]);
  if (idx < 0) { r->r = vm.undef(); return; }
  *r = boxValue(vm, m->entries[idx].v, m->vvt);
}
template <Kind K, Kind V>
static void m_getStrict(VM& vm, Slot* a, uint32_t, Slot* r, void*) {
  MapObj* m = Mo(vm, a[0]);
  checkK<K>(vm, m);
  if (kindOfVT(m->vvt) != V) vm.error("map value type mismatch");
  int32_t idx = mapFind(vm, m, a[1]);
  if (idx < 0) vm.error("map key not found (value is undefined)");
  *r = m->entries[idx].v;
}
template <Kind K, Kind V>
static void m_getOr(VM& vm, Slot* a, uint32_t, Slot* r, void*) {
  MapObj* m = Mo(vm, a[0]);
  checkK<K>(vm, m);
  if (kindOfVT(m->vvt) != V) vm.error("map value type mismatch");
  int32_t idx = mapFind(vm, m, a[1]);
  *r = idx < 0 ? a[2] : m->entries[idx].v;
}
template <Kind K, Kind V>
static void m_set(VM& vm, Slot* a, uint32_t, Slot* r, void*) {
  MapObj* m = Mo(vm, a[0]);
  checkK<K>(vm, m);
  if (m->isSet || kindOfVT(m->vvt) != V) vm.error("map value type mismatch");
  mapSet(vm, m, a[1], a[2]);
  r->r = m;
}
template <Kind K>
static void s_add(VM& vm, Slot* a, uint32_t, Slot* r, void*) {
  MapObj* m = Mo(vm, a[0]);
  checkK<K>(vm, m);
  Slot z;
  z.u = 0;
  if (mapFind(vm, m, a[1]) < 0) mapSet(vm, m, a[1], z);
  r->r = m;
}
template <Kind K>
static void m_has(VM& vm, Slot* a, uint32_t, Slot* r, void*) {
  MapObj* m = Mo(vm, a[0]);
  checkK<K>(vm, m);
  r->i = mapFind(vm, m, a[1]) >= 0;
}
template <Kind K>
static void m_delete(VM& vm, Slot* a, uint32_t, Slot* r, void*) {
  MapObj* m = Mo(vm, a[0]);
  checkK<K>(vm, m);
  r->i = mapDelete(vm, m, a[1]);
}
E7_NATIVE(m_clear) {
  E7_UNUSED;
  MapObj* m = Mo(vm, a[0]);
  size_t before = mapBytes(m);
  m->entries.clear();
  m->entries.shrink_to_fit();
  m->buckets.clear();
  m->buckets.shrink_to_fit();
  m->count = 0;
  accountMap(vm, before, mapBytes(m));
}
E7_NATIVE(m_copy) {
  E7_UNUSED;
  MapObj* src = Mo(vm, a[0]);
  MapObj* m = vm.newMap(src->kvt, src->vvt, src->km, src->isSet);
  for (auto& e : src->entries)
    if (e.live) mapSet(vm, m, e.k, e.v);
  r->r = m;
}
E7_NATIVE(m_size) { E7_UNUSED; r->f = Mo(vm, a[0])->count; }
E7_NATIVE(m_keys) {
  E7_UNUSED;
  MapObj* m = Mo(vm, a[0]);
  ArrObj* arr = vm.newArray(m->kvt, m->count);
  for (auto& e : m->entries)
    if (e.live) arr->data[arr->len++] = e.k;
  r->r = arr;
}
E7_NATIVE(m_values) {
  E7_UNUSED;
  MapObj* m = Mo(vm, a[0]);
  ArrObj* arr = vm.newArray(m->vvt, m->count);
  for (auto& e : m->entries)
    if (e.live) arr->data[arr->len++] = e.v;
  r->r = arr;
}
E7_NATIVE(m_entries) {
  E7_UNUSED;
  MapObj* m = Mo(vm, a[0]);
  ModuleRT* mod = currentModule(vm);
  double ci = a[1].f;
  if (!mod || !(ci >= 0 && ci < mod->classes.size())) vm.error("invalid tuple class");
  ClassRT* cls = &mod->classes[(size_t)ci];
  uint8_t vvt = m->isSet ? m->kvt : m->vvt;
  if (cls->nfields != 2 || kindOfVT(cls->fieldVT[0]) != kindOfVT(m->kvt) || kindOfVT(cls->fieldVT[1]) != kindOfVT(vvt))
    vm.error("invalid tuple class for map entries");
  ArrObj* arr = vm.newArray(VT_REF, m->count);
  for (auto& e : m->entries) {
    if (!e.live) continue;
    InstObj* t = vm.newInstance(cls);
    t->fields()[0] = e.k;
    t->fields()[1] = m->isSet ? e.k : e.v;
    arr->data[arr->len++].r = t;
  }
  r->r = arr;
}
// Iteration cursors are insertion numbers (MapEntry::seq), not vector indices: compaction (rehash after
// deletions) and clear() move entries, but an iteration must still visit every entry inserted after
// its current position, as in JavaScript. entries stay sorted by seq.
static size_t lowerBoundSeq(const MapObj* m, int64_t s) {
  const auto& E = m->entries;
  if (E.empty()) return 0;
  int64_t guess = s - E[0].seq;  // no compaction since: seq - first seq is the index
  if (guess >= 0 && guess < (int64_t)E.size() && E[(size_t)guess].seq == s) return (size_t)guess;
  size_t lo = 0, hi = E.size();
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (E[mid].seq < s) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}
static const MapEntry& entryAtSeq(VM& vm, const MapObj* m, int64_t s) {
  size_t i = lowerBoundSeq(m, s);
  if (i >= m->entries.size() || m->entries[i].seq != s || !m->entries[i].live) vm.error("map iterator out of range");
  return m->entries[i];
}
E7_NATIVE(m_next) {
  E7_UNUSED;
  MapObj* m = Mo(vm, a[0]);
  int64_t s = a[1].i;
  if (s < 0) s = 0;
  for (size_t i = lowerBoundSeq(m, s); i < m->entries.size(); i++)
    if (m->entries[i].live) { r->i = m->entries[i].seq; return; }
  r->i = -1;
}
template <Kind K>
static void m_keyAt(VM& vm, Slot* a, uint32_t, Slot* r, void*) {
  MapObj* m = Mo(vm, a[0]);
  checkK<K>(vm, m);
  *r = entryAtSeq(vm, m, a[1].i).k;
}
template <Kind V>
static void m_valAt(VM& vm, Slot* a, uint32_t, Slot* r, void*) {
  MapObj* m = Mo(vm, a[0]);
  if (kindOfVT(m->vvt) != V) vm.error("map value type mismatch");
  *r = entryAtSeq(vm, m, a[1].i).v;
}

template <Kind K, Kind V>
static void regKV(VM& vm) {
  std::string k(1, kindChar(K)), v(1, kindChar(V));
  vm.registerNative("Map.getStrict" + k + v, "R" + k + ">" + v, m_getStrict<K, V>);
  vm.registerNative("Map.getOr" + k + v, "R" + k + v + ">" + v, m_getOr<K, V>);
  vm.registerNative("Map.set" + k + v, "R" + k + v + ">R", m_set<K, V>);
}
template <Kind K>
static void regK(VM& vm) {
  std::string k(1, kindChar(K));
  vm.registerNative("Map.get" + k, "R" + k + ">R", m_get<K>);
  vm.registerNative("Map.has" + k, "R" + k + ">I", m_has<K>);
  vm.registerNative("Map.delete" + k, "R" + k + ">I", m_delete<K>);
  vm.registerNative("Set.add" + k, "R" + k + ">R", s_add<K>);
  vm.registerNative("Map.keyAt" + k, "RI>" + k, m_keyAt<K>);
  vm.registerNative("Map.valAt" + k, "RI>" + k, m_valAt<K>);
  regKV<K, K_I>(vm);
  regKV<K, K_F>(vm);
  regKV<K, K_R>(vm);
}

void registerMapNatives(VM& vm) {
  vm.registerNative("Map.new", "FFF>R", m_new);
  vm.registerNative("Map.fromEntries", "RFFF>R", m_fromEntries);
  vm.registerNative("Set.new", "FF>R", s_new);
  vm.registerNative("Set.fromArray", "RFF>R", s_fromArray);
  vm.registerNative("Map.clear", "R>V", m_clear);
  vm.registerNative("Map.size", "R>F", m_size);
  vm.registerNative("Map.copy", "R>R", m_copy);
  vm.registerNative("Map.keys", "R>R", m_keys);
  vm.registerNative("Map.values", "R>R", m_values);
  vm.registerNative("Map.entries", "RF>R", m_entries);
  vm.registerNative("Map.next", "RI>I", m_next);
  regK<K_I>(vm);
  regK<K_F>(vm);
  regK<K_R>(vm);
}

}  // namespace e7
