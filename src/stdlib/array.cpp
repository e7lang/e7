// Array natives. Element storage is typed (Slot + array value type).
#include <algorithm>
#include <functional>

#include "util.h"

namespace e7 {

static Slot boxElem(VM& vm, ArrObj* arr, Slot v) { return boxValue(vm, v, arr->vt); }

E7_NATIVE(a_pushArray) {
  E7_UNUSED;
  ArrObj* d = A(vm, a[0]);
  ArrObj* s = A(vm, a[1]);
  if (kindOfVT(d->vt) != kindOfVT(s->vt)) vm.error("array element type mismatch");
  uint32_t n0 = s->len;
  vm.arrayReserve(d, d->len + n0);
  for (uint32_t i = 0; i < n0; i++) d->data[d->len++] = s->data[i];
  r->f = d->len;
}
E7_NATIVE(a_pop) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  if (!arr->len) { r->r = vm.undef(); return; }
  Slot v = arr->data[--arr->len];
  r->r = boxElem(vm, arr, v).r;
}
E7_NATIVE(a_shift) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  if (!arr->len) { r->r = vm.undef(); return; }
  Slot v = arr->data[0];
  memmove(arr->data, arr->data + 1, (size_t)(arr->len - 1) * sizeof(Slot));
  arr->len--;
  r->r = boxElem(vm, arr, v).r;
}
template <Kind K>
static void a_popStrict(VM& vm, Slot* a, uint32_t, Slot* r, void*) {
  ArrObj* arr = A(vm, a[0]);
  if (kindOfVT(arr->vt) != K) vm.error("array element type mismatch");
  if (!arr->len) vm.error("pop() on an empty array (value is undefined)");
  *r = arr->data[--arr->len];
}
template <Kind K>
static void a_shiftStrict(VM& vm, Slot* a, uint32_t, Slot* r, void*) {
  ArrObj* arr = A(vm, a[0]);
  if (kindOfVT(arr->vt) != K) vm.error("array element type mismatch");
  if (!arr->len) vm.error("shift() on an empty array (value is undefined)");
  *r = arr->data[0];
  memmove(arr->data, arr->data + 1, (size_t)(arr->len - 1) * sizeof(Slot));
  arr->len--;
}
template <Kind K>
static void a_unshift(VM& vm, Slot* a, uint32_t, Slot* r, void*) {
  ArrObj* arr = A(vm, a[0]);
  if (kindOfVT(arr->vt) != K) vm.error("array element type mismatch");
  vm.arrayReserve(arr, arr->len + 1);
  memmove(arr->data + 1, arr->data, (size_t)arr->len * sizeof(Slot));
  arr->data[0] = a[1];
  arr->len++;
  r->f = arr->len;
}
E7_NATIVE(a_at) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  double i = toInteger(a[1].f);
  if (i < 0) i += arr->len;
  if (i < 0 || i >= arr->len) { r->r = vm.undef(); return; }
  r->r = boxElem(vm, arr, arr->data[(size_t)i]).r;
}
E7_NATIVE(a_slice) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  int64_t b = relIndex(a[1].f, arr->len), e = relIndex(a[2].f, arr->len);
  uint32_t n0 = e > b ? (uint32_t)(e - b) : 0;
  ArrObj* out = vm.newArray(arr->vt, n0);
  if (n0) memcpy(out->data, arr->data + b, n0 * sizeof(Slot));
  out->len = n0;
  r->r = out;
}
E7_NATIVE(a_splice) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  int64_t len = arr->len;
  int64_t start = relIndex(a[1].f, len);
  double dcd = toInteger(a[2].f);
  int64_t dc = dcd < 0 ? 0 : (dcd > (double)(len - start) ? len - start : (int64_t)dcd);
  ArrObj* items = nullptr;
  if (a[3].r) {
    items = A(vm, a[3]);
    if (kindOfVT(items->vt) != kindOfVT(arr->vt)) vm.error("array element type mismatch");
  }
  uint32_t ni = items ? items->len : 0;
  ArrObj* removed = vm.newArray(arr->vt, (uint32_t)dc);
  if (dc) memcpy(removed->data, arr->data + start, (size_t)dc * sizeof(Slot));
  removed->len = (uint32_t)dc;
  std::vector<Slot> ins;
  if (ni) ins.assign(items->data, items->data + ni);
  int64_t newLen = len - dc + ni;
  if (newLen > 0x7FFFFFF) vm.error("array too large");
  vm.arrayReserve(arr, (uint32_t)newLen);
  // nothing to move when no element follows the removed ones (data is null for an empty array: memmove of null is UB)
  if (len - start - dc > 0)
    memmove(arr->data + start + ni, arr->data + start + dc, (size_t)(len - start - dc) * sizeof(Slot));
  if (ni) memcpy(arr->data + start, ins.data(), ni * sizeof(Slot));
  arr->len = (uint32_t)newLen;
  r->r = removed;
}
E7_NATIVE(a_concat) {
  E7_UNUSED;
  ArrObj* x = A(vm, a[0]);
  ArrObj* y = A(vm, a[1]);
  if (kindOfVT(x->vt) != kindOfVT(y->vt)) vm.error("array element type mismatch");
  uint64_t total = (uint64_t)x->len + y->len;
  if (total > 0x7FFFFFF) vm.error("array too large");
  ArrObj* out = vm.newArray(x->vt, (uint32_t)total);
  if (x->len) memcpy(out->data, x->data, x->len * sizeof(Slot));
  if (y->len) memcpy(out->data + x->len, y->data, y->len * sizeof(Slot));
  out->len = (uint32_t)total;
  r->r = out;
}
template <Kind K>
static void a_indexOf(VM& vm, Slot* a, uint32_t, Slot* r, void*) {
  ArrObj* arr = A(vm, a[0]);
  if (kindOfVT(arr->vt) != K) vm.error("array element type mismatch");
  int64_t from = relIndex(a[2].f, arr->len);
  for (int64_t i = from; i < arr->len; i++)
    if (strictEquals(vm, arr->data[i], a[1], arr->vt)) { r->f = (double)i; return; }
  r->f = -1;
}
template <Kind K>
static void a_lastIndexOf(VM& vm, Slot* a, uint32_t, Slot* r, void*) {
  ArrObj* arr = A(vm, a[0]);
  if (kindOfVT(arr->vt) != K) vm.error("array element type mismatch");
  double fd = toInteger(a[2].f);
  double fr = fd < 0 ? fd + arr->len : (fd >= arr->len ? (double)arr->len - 1 : fd);
  int64_t from = fr < 0 ? -1 : (int64_t)fr;  // (no conversion of out-of-range doubles)
  for (int64_t i = from; i >= 0; i--)
    if (strictEquals(vm, arr->data[i], a[1], arr->vt)) { r->f = (double)i; return; }
  r->f = -1;
}
template <Kind K>
static void a_includes(VM& vm, Slot* a, uint32_t, Slot* r, void*) {
  ArrObj* arr = A(vm, a[0]);
  if (kindOfVT(arr->vt) != K) vm.error("array element type mismatch");
  for (uint32_t i = 0; i < arr->len; i++)
    if (sameValueZero(vm, arr->data[i], a[1], arr->vt)) { r->i = 1; return; }
  r->i = 0;
}
E7_NATIVE(a_join) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  StrObj* sep = S(vm, a[1]);
  std::u16string out;
  for (uint32_t i = 0; i < arr->len; i++) {
    if (i) out.append(sep->chars(), sep->len);
    Slot e = arr->data[i];
    if (arr->vt == VT_REF && (!e.r || e.r == vm.undef())) continue;
    out += valueToString(vm, e, arr->vt);
    if (out.size() > 0x3FFFFFFF) vm.error("RangeError: invalid string length");
  }
  r->r = vm.newString(out);
}
E7_NATIVE(a_reverse) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  std::reverse(arr->data, arr->data + arr->len);
  r->r = arr;
}
template <Kind K>
static void a_fill(VM& vm, Slot* a, uint32_t, Slot* r, void*) {
  ArrObj* arr = A(vm, a[0]);
  if (kindOfVT(arr->vt) != K) vm.error("array element type mismatch");
  int64_t b = relIndex(a[2].f, arr->len), e = relIndex(a[3].f, arr->len);
  for (int64_t i = b; i < e; i++) arr->data[i] = a[1];
  r->r = arr;
}

// Stable merge sort that stays memory-safe with inconsistent comparators.
static void mergeSort(std::vector<Slot>& v, const std::function<bool(const Slot&, const Slot&)>& less) {
  size_t n = v.size();
  if (n < 2) return;
  std::vector<Slot> tmp(n);
  for (size_t width = 1; width < n; width *= 2) {
    for (size_t lo = 0; lo < n; lo += 2 * width) {
      size_t mid = std::min(lo + width, n), hi = std::min(lo + 2 * width, n);
      size_t i = lo, j = mid, k = lo;
      while (i < mid && j < hi) {
        if (less(v[j], v[i])) tmp[k++] = v[j++];
        else tmp[k++] = v[i++];
      }
      while (i < mid) tmp[k++] = v[i++];
      while (j < hi) tmp[k++] = v[j++];
    }
    v.swap(tmp);
  }
}

E7_NATIVE(a_sort) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  // default sort: by UTF-16 string comparison, undefined last
  struct Item { std::u16string key; Slot v; bool undef; };
  // the keys are a transient buffer bounded by the memory limit (one long string referenced many times
  // makes keys far larger than the heap), and comparing them is charged by their length
  size_t bytes = (size_t)arr->len * sizeof(Item), chars = 0;
  vm.checkTemp(bytes);
  std::vector<Item> items;
  items.reserve(arr->len);
  for (uint32_t i = 0; i < arr->len; i++) {
    Slot e = arr->data[i];
    bool und = arr->vt == VT_REF && e.r == vm.undef();
    items.push_back({und ? u"" : valueToString(vm, e, arr->vt), e, und});
    chars += items.back().key.size();
    bytes += items.back().key.size() * sizeof(char16_t);
    vm.checkTemp(bytes);
  }
  uint64_t lg = 1;
  while ((1ull << lg) < arr->len) lg++;
  vm.chargeWork(chars / 8 * lg);
  std::stable_sort(items.begin(), items.end(), [](const Item& x, const Item& y) {
    if (x.undef != y.undef) return !x.undef;
    return x.key < y.key;
  });
  for (uint32_t i = 0; i < arr->len && i < items.size(); i++) arr->data[i] = items[i].v;
  r->r = arr;
}
E7_NATIVE(a_sortCmp) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  CloObj* c = Clo(vm, a[1]);
  char k = kindChar(kindOfVT(arr->vt));
  std::string want = std::string(1, k) + k + ">F";
  if (vm.sigString(c->fn->sigId) != want) vm.error("sort comparator has the wrong signature");
  vm.checkTemp((size_t)arr->len * 3 * sizeof(Slot));  // v, mergeSort's buffer and the temporary roots
  std::vector<Slot> v(arr->data, arr->data + arr->len);
  // the comparator runs script code, during which the collector may run: the elements being sorted
  // stay reachable even if the comparator empties or shrinks the array (arr and c are reachable
  // through the caller's registers)
  VM::TempRootSlots keep(vm, v.data(), v.size(), arr->vt == VT_REF);
  bool undefLast = arr->vt == VT_REF;
  Obj* und = vm.undef();
  mergeSort(v, [&](const Slot& x, const Slot& y) {
    if (undefLast) {
      bool ux = x.r == und, uy = y.r == und;
      if (ux || uy) return !ux && uy;
    }
    Slot args[2] = {x, y};
    Slot res = vm.callClosure(c, args, 2);
    return res.f < 0;
  });
  uint32_t n0 = std::min<uint32_t>(arr->len, (uint32_t)v.size());
  for (uint32_t i = 0; i < n0; i++) arr->data[i] = v[i];
  r->r = arr;
}
// JavaScript rejects lengths that are not integers in [0, 2^32 - 1] with a RangeError; e7 also
// limits arrays to 2^27 - 1 elements, which is its own limit and therefore not catchable.
static void checkArrayLength(VM& vm, double d) {
  if (!(d >= 0 && d <= 4294967295.0) || std::trunc(d) != d) vm.error("RangeError: invalid array length");
  if (d > 0x7FFFFFF) vm.error("array length %s exceeds the e7 limit of 134217727", numberToString(d).c_str());
}

E7_NATIVE(a_newLen) {
  E7_UNUSED;
  double d = a[0].f;
  checkArrayLength(vm, d);
  int vti = immIndex(a[1].f, VT__COUNT);
  if (vti < 0) vm.error("invalid element type");
  uint8_t vt = (uint8_t)vti;
  ArrObj* arr = vm.newArray(vt, (uint32_t)d);
  arr->len = (uint32_t)d;
  if (vt == VT_REF)
    for (uint32_t i = 0; i < arr->len; i++) arr->data[i].r = vm.undef();
  r->r = arr;
}
// ES2023 copying methods: a copy of the array, then the mutating operation on the copy
static ArrObj* copyOf(VM& vm, ArrObj* arr) {
  ArrObj* out = vm.newArray(arr->vt, arr->len);
  if (arr->len) memcpy(out->data, arr->data, (size_t)arr->len * sizeof(Slot));
  out->len = arr->len;
  return out;
}
E7_NATIVE(a_toReversed) {
  ArrObj* c = copyOf(vm, A(vm, a[0]));
  VM::TempRoot keep(vm, c);
  Slot s[1];
  s[0].r = c;
  a_reverse(vm, s, 1, r, ud);
  (void)n;
}
E7_NATIVE(a_toSorted) {
  ArrObj* c = copyOf(vm, A(vm, a[0]));
  VM::TempRoot keep(vm, c);
  Slot s[1];
  s[0].r = c;
  a_sort(vm, s, 1, r, ud);
  (void)n;
}
E7_NATIVE(a_toSortedCmp) {
  ArrObj* c = copyOf(vm, A(vm, a[0]));
  VM::TempRoot keep(vm, c);
  Slot s[2];
  s[0].r = c;
  s[1] = a[1];
  a_sortCmp(vm, s, 2, r, ud);
  (void)n;
}
E7_NATIVE(a_toSpliced) {
  ArrObj* c = copyOf(vm, A(vm, a[0]));
  VM::TempRoot keep(vm, c);
  Slot s[4];
  s[0].r = c;
  s[1] = a[1];
  s[2] = a[2];
  s[3] = a[3];
  Slot removed;
  a_splice(vm, s, 4, &removed, ud);
  r->r = c;
  (void)n;
}
// arr.with(index, value): a copy with one element replaced (a RangeError outside the array)
E7_NATIVE(a_with) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  double d = a[1].f;
  double t = std::isnan(d) ? 0 : std::trunc(d);
  double idx = t < 0 ? t + arr->len : t;
  if (!(idx >= 0 && idx < arr->len)) vm.error("RangeError: Invalid index : %s", numberToString(t).c_str());
  ArrObj* c = copyOf(vm, arr);
  c->data[(uint32_t)idx] = a[2];
  r->r = c;
}

E7_NATIVE(a_setLength) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  double d = a[1].f;
  checkArrayLength(vm, d);
  uint32_t nl = (uint32_t)d;
  if (nl > arr->len) {
    vm.arrayReserve(arr, nl);
    for (uint32_t i = arr->len; i < nl; i++) {
      arr->data[i].u = 0;
      if (arr->vt == VT_REF) arr->data[i].r = vm.undef();
    }
  }
  arr->len = nl;
}

void registerArrayNatives(VM& vm) {
  vm.registerNative("Array.pushArray", "RR>F", a_pushArray);
  vm.registerNative("Array.pop", "R>R", a_pop);
  vm.registerNative("Array.shift", "R>R", a_shift);
  vm.registerNative("Array.popStrictI", "R>I", a_popStrict<K_I>);
  vm.registerNative("Array.popStrictF", "R>F", a_popStrict<K_F>);
  vm.registerNative("Array.popStrictR", "R>R", a_popStrict<K_R>);
  vm.registerNative("Array.shiftStrictI", "R>I", a_shiftStrict<K_I>);
  vm.registerNative("Array.shiftStrictF", "R>F", a_shiftStrict<K_F>);
  vm.registerNative("Array.shiftStrictR", "R>R", a_shiftStrict<K_R>);
  vm.registerNative("Array.unshiftI", "RI>F", a_unshift<K_I>);
  vm.registerNative("Array.unshiftF", "RF>F", a_unshift<K_F>);
  vm.registerNative("Array.unshiftR", "RR>F", a_unshift<K_R>);
  vm.registerNative("Array.at", "RF>R", a_at);
  vm.registerNative("Array.slice", "RFF>R", a_slice);
  vm.registerNative("Array.splice", "RFFR>R", a_splice);
  vm.registerNative("Array.concat", "RR>R", a_concat);
  vm.registerNative("Array.indexOfI", "RIF>F", a_indexOf<K_I>);
  vm.registerNative("Array.indexOfF", "RFF>F", a_indexOf<K_F>);
  vm.registerNative("Array.indexOfR", "RRF>F", a_indexOf<K_R>);
  vm.registerNative("Array.lastIndexOfI", "RIF>F", a_lastIndexOf<K_I>);
  vm.registerNative("Array.lastIndexOfF", "RFF>F", a_lastIndexOf<K_F>);
  vm.registerNative("Array.lastIndexOfR", "RRF>F", a_lastIndexOf<K_R>);
  vm.registerNative("Array.includesI", "RI>I", a_includes<K_I>);
  vm.registerNative("Array.includesF", "RF>I", a_includes<K_F>);
  vm.registerNative("Array.includesR", "RR>I", a_includes<K_R>);
  vm.registerNative("Array.join", "RR>R", a_join);
  vm.registerNative("Array.reverse", "R>R", a_reverse);
  vm.registerNative("Array.fillI", "RIFF>R", a_fill<K_I>);
  vm.registerNative("Array.fillF", "RFFF>R", a_fill<K_F>);
  vm.registerNative("Array.fillR", "RRFF>R", a_fill<K_R>);
  vm.registerNative("Array.sort", "R>R", a_sort);
  vm.registerNative("Array.sortCmp", "RR>R", a_sortCmp);
  vm.registerNative("Array.toReversed", "R>R", a_toReversed);
  vm.registerNative("Array.toSorted", "R>R", a_toSorted);
  vm.registerNative("Array.toSortedCmp", "RR>R", a_toSortedCmp);
  vm.registerNative("Array.toSpliced", "RFFR>R", a_toSpliced);
  vm.registerNative("Array.withI", "RFI>R", a_with);
  vm.registerNative("Array.withF", "RFF>R", a_with);
  vm.registerNative("Array.withR", "RFR>R", a_with);
  vm.registerNative("Array.newLen", "FF>R", a_newLen);
  vm.registerNative("Array.setLength", "RF>V", a_setLength);
}

}  // namespace e7
