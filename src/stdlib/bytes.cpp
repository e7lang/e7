// Uint8Array / ArrayBuffer / DataView natives.
#include <algorithm>

#include "util.h"

namespace e7 {

// a length argument: ToIndex (2.5 is 2, NaN is 0; negative and infinite lengths are errors)
static uint32_t checkLen(VM& vm, double d) {
  if (d != d) d = 0;
  d = std::trunc(d);
  if (d < 0 || std::isinf(d)) vm.error("RangeError: Invalid typed array length: %s", numberToString(d).c_str());
  if (d > 0x7FFFFFFF) vm.error("typed array length %s exceeds the e7 limit of 2147483647", numberToString(d).c_str());
  return (uint32_t)d;
}

E7_NATIVE(b_new) { E7_UNUSED; r->r = vm.newBytes(checkLen(vm, a[0].f)); }
E7_NATIVE(b_fromArray) {
  E7_UNUSED;
  ArrObj* arr = A(vm, a[0]);
  if (arr->vt != VT_NUMBER) vm.error("expected number[]");
  BytesObj* b = vm.newBytes(arr->len);
  for (uint32_t i = 0; i < arr->len; i++) b->ptr()[i] = (uint8_t)doubleToInt64Bits(arr->data[i].f);
  r->r = b;
}
E7_NATIVE(b_copy) {
  E7_UNUSED;
  BytesObj* s = B(vm, a[0]);
  r->r = vm.newBytesFrom(s->ptr(), s->len);
}
E7_NATIVE(b_toArray) {
  E7_UNUSED;
  BytesObj* s = B(vm, a[0]);
  ArrObj* arr = vm.newArray(VT_NUMBER, s->len);
  for (uint32_t i = 0; i < s->len; i++) arr->data[i].f = s->ptr()[i];
  arr->len = s->len;
  r->r = arr;
}
// view over an ArrayBuffer (Uint8Array / DataView constructors)
E7_NATIVE(b_view) {
  E7_UNUSED;
  BufObj* buf = Buf(vm, a[0]);
  double off = toInteger(a[1].f);
  if (off < 0 || off > buf->len) vm.error("RangeError: start offset %s is outside the bounds of the buffer", numberToString(a[1].f).c_str());
  double len;
  if (a[2].f != a[2].f) len = buf->len - off;
  else {
    len = toInteger(a[2].f);
    if (len < 0 || off + len > buf->len) vm.error("RangeError: invalid length %s", numberToString(a[2].f).c_str());
  }
  r->r = vm.newBytesView(buf, (uint32_t)off, (uint32_t)len);
}
E7_NATIVE(b_byteOffset) { E7_UNUSED; r->f = B(vm, a[0])->off; }
E7_NATIVE(b_buffer) { E7_UNUSED; r->r = B(vm, a[0])->buf; }
E7_NATIVE(b_set) {
  E7_UNUSED;
  BytesObj* d = B(vm, a[0]);
  double off = toInteger(a[2].f);
  Obj* src = a[1].r;
  if (!src) vm.error("set source is null");
  uint32_t n0 = src->type == OT::Bytes ? static_cast<BytesObj*>(src)->len : src->type == OT::Array ? static_cast<ArrObj*>(src)->len : 0;
  if (src->type != OT::Bytes && src->type != OT::Array) vm.error("invalid set source");
  if (off < 0 || off + n0 > d->len) vm.error("RangeError: offset is out of bounds");
  uint32_t o = (uint32_t)off;
  if (src->type == OT::Bytes) memmove(d->ptr() + o, static_cast<BytesObj*>(src)->ptr(), n0);
  else {
    auto* arr = static_cast<ArrObj*>(src);
    if (arr->vt != VT_NUMBER) vm.error("expected number[]");
    for (uint32_t i = 0; i < n0; i++) d->ptr()[o + i] = (uint8_t)doubleToInt64Bits(arr->data[i].f);
  }
}
E7_NATIVE(b_subarray) {
  E7_UNUSED;
  BytesObj* s = B(vm, a[0]);
  int64_t b = relIndex(a[1].f, s->len), e = relIndex(a[2].f, s->len);
  if (e < b) e = b;
  r->r = vm.newBytesView(s->buf, s->off + (uint32_t)b, (uint32_t)(e - b));
}
E7_NATIVE(b_slice) {
  E7_UNUSED;
  BytesObj* s = B(vm, a[0]);
  int64_t b = relIndex(a[1].f, s->len), e = relIndex(a[2].f, s->len);
  if (e < b) e = b;
  r->r = vm.newBytesFrom(s->ptr() + b, (size_t)(e - b));
}
E7_NATIVE(b_fill) {
  E7_UNUSED;
  BytesObj* s = B(vm, a[0]);
  uint8_t v = (uint8_t)doubleToInt64Bits(a[1].f);
  int64_t b = relIndex(a[2].f, s->len), e = relIndex(a[3].f, s->len);
  if (e > b) memset(s->ptr() + b, v, (size_t)(e - b));
  r->r = s;
}
static double findByte(BytesObj* s, double v, int64_t from, bool reverse) {
  if (v != v || std::trunc(v) != v || v < 0 || v > 255) return -1;
  uint8_t x = (uint8_t)v;
  if (reverse) {
    for (int64_t i = from; i >= 0; i--) if (s->ptr()[i] == x) return (double)i;
  } else {
    for (int64_t i = from; i < s->len; i++) if (s->ptr()[i] == x) return (double)i;
  }
  return -1;
}
E7_NATIVE(b_indexOf) { E7_UNUSED; BytesObj* s = B(vm, a[0]); r->f = findByte(s, a[1].f, relIndex(a[2].f, s->len), false); }
E7_NATIVE(b_includes) { E7_UNUSED; BytesObj* s = B(vm, a[0]); r->i = findByte(s, a[1].f, relIndex(a[2].f, s->len), false) >= 0; }
E7_NATIVE(b_lastIndexOf) {
  E7_UNUSED;
  BytesObj* s = B(vm, a[0]);
  double fd = toInteger(a[2].f);
  double fr = fd < 0 ? fd + s->len : (fd >= s->len ? (double)s->len - 1 : fd);
  int64_t from = fr < 0 ? -1 : (int64_t)fr;
  r->f = findByte(s, a[1].f, from, true);
}
E7_NATIVE(b_join) {
  E7_UNUSED;
  BytesObj* s = B(vm, a[0]);
  StrObj* sep = S(vm, a[1]);
  std::u16string out;
  for (uint32_t i = 0; i < s->len; i++) {
    if (i) out.append(sep->chars(), sep->len);
    std::string d = std::to_string(s->ptr()[i]);
    out.append(d.begin(), d.end());
  }
  r->r = vm.newString(out);
}
E7_NATIVE(b_reverse) {
  E7_UNUSED;
  BytesObj* s = B(vm, a[0]);
  std::reverse(s->ptr(), s->ptr() + s->len);
  r->r = s;
}
E7_NATIVE(b_at) {
  E7_UNUSED;
  BytesObj* s = B(vm, a[0]);
  double i = toInteger(a[1].f);
  if (i < 0) i += s->len;
  if (i < 0 || i >= s->len) { r->r = vm.undef(); return; }
  Slot v;
  v.f = s->ptr()[(size_t)i];
  r->r = vm.newBox(VT_NUMBER, v);
}
E7_NATIVE(buf_new) { E7_UNUSED; r->r = vm.newBuffer(checkLen(vm, a[0].f)); }
E7_NATIVE(buf_byteLength) { E7_UNUSED; r->f = Buf(vm, a[0])->len; }
E7_NATIVE(buf_slice) {
  E7_UNUSED;
  BufObj* b = Buf(vm, a[0]);
  int64_t s = relIndex(a[1].f, b->len), e = relIndex(a[2].f, b->len);
  if (e < s) e = s;
  BufObj* nb = vm.newBuffer((uint32_t)(e - s));
  if (e > s) memcpy(nb->data, b->data + s, (size_t)(e - s));
  r->r = nb;
}

// DataView: type codes 0 Uint8 1 Int8 2 Uint16 3 Int16 4 Uint32 5 Int32 6 Float32 7 Float64
static const int kSize[8] = {1, 1, 2, 2, 4, 4, 4, 8};
static uint8_t* dvPtr(VM& vm, BytesObj* v, double off, int size) {
  double o = toInteger(off);
  if (o < 0 || o + size > v->len) vm.error("RangeError: offset is outside the bounds of the DataView");
  return v->ptr() + (size_t)o;
}
static uint64_t loadN(const uint8_t* p, int n, bool le) {
  uint64_t v = 0;
  for (int i = 0; i < n; i++) v |= (uint64_t)p[le ? i : n - 1 - i] << (8 * i);
  return v;
}
static void storeN(uint8_t* p, int n, uint64_t v, bool le) {
  for (int i = 0; i < n; i++) p[le ? i : n - 1 - i] = (uint8_t)(v >> (8 * i));
}
E7_NATIVE(dv_get) {
  E7_UNUSED;
  BytesObj* v = B(vm, a[0]);
  int t = immIndex(a[2].f, 8);
  if (t < 0) vm.error("invalid DataView type");
  bool le = a[3].i != 0;
  uint64_t raw = loadN(dvPtr(vm, v, a[1].f, kSize[t]), kSize[t], le);
  switch (t) {
    case 0: r->f = (uint8_t)raw; break;
    case 1: r->f = (int8_t)raw; break;
    case 2: r->f = (uint16_t)raw; break;
    case 3: r->f = (int16_t)raw; break;
    case 4: r->f = (uint32_t)raw; break;
    case 5: r->f = (int32_t)raw; break;
    case 6: { uint32_t u = (uint32_t)raw; float f; memcpy(&f, &u, 4); r->f = f; break; }
    default: { double d; memcpy(&d, &raw, 8); r->f = d; break; }
  }
}
E7_NATIVE(dv_set) {
  E7_UNUSED;
  BytesObj* v = B(vm, a[0]);
  int t = immIndex(a[3].f, 8);
  if (t < 0) vm.error("invalid DataView type");
  bool le = a[4].i != 0;
  double x = a[2].f;
  uint64_t raw;
  if (t == 6) { float f = (float)x; uint32_t u; memcpy(&u, &f, 4); raw = u; }
  else if (t == 7) memcpy(&raw, &x, 8);
  else raw = (uint64_t)doubleToInt64Bits(x);
  storeN(dvPtr(vm, v, a[1].f, kSize[t]), kSize[t], raw, le);
}
E7_NATIVE(dv_getBig) {
  E7_UNUSED;
  BytesObj* v = B(vm, a[0]);
  bool sgn = a[2].i != 0, le = a[3].i != 0;
  uint64_t raw = loadN(dvPtr(vm, v, a[1].f, 8), 8, le);
  if (sgn && (int64_t)raw < 0) r->r = bigFromU64(vm, 0 - raw, true);
  else r->r = bigFromU64(vm, raw, false);
}
E7_NATIVE(dv_setBig) {
  E7_UNUSED;
  BytesObj* v = B(vm, a[0]);
  bool le = a[4].i != 0;
  storeN(dvPtr(vm, v, a[1].f, 8), 8, bigToU64(Big(vm, a[2])), le);
}

void registerBytesNatives(VM& vm) {
  vm.registerNative("Bytes.new", "F>R", b_new);
  vm.registerNative("Bytes.fromArray", "R>R", b_fromArray);
  vm.registerNative("Bytes.copy", "R>R", b_copy);
  vm.registerNative("Bytes.toArray", "R>R", b_toArray);
  vm.registerNative("Bytes.view", "RFF>R", b_view);
  vm.registerNative("Bytes.byteOffset", "R>F", b_byteOffset);
  vm.registerNative("Bytes.buffer", "R>R", b_buffer);
  vm.registerNative("Bytes.set", "RRF>V", b_set);
  vm.registerNative("Bytes.subarray", "RFF>R", b_subarray);
  vm.registerNative("Bytes.slice", "RFF>R", b_slice);
  vm.registerNative("Bytes.fill", "RFFF>R", b_fill);
  vm.registerNative("Bytes.indexOf", "RFF>F", b_indexOf);
  vm.registerNative("Bytes.lastIndexOf", "RFF>F", b_lastIndexOf);
  vm.registerNative("Bytes.includes", "RFF>I", b_includes);
  vm.registerNative("Bytes.join", "RR>R", b_join);
  vm.registerNative("Bytes.reverse", "R>R", b_reverse);
  vm.registerNative("Bytes.at", "RF>R", b_at);
  vm.registerNative("Buffer.new", "F>R", buf_new);
  vm.registerNative("Buffer.byteLength", "R>F", buf_byteLength);
  vm.registerNative("Buffer.slice", "RFF>R", buf_slice);
  vm.registerNative("DataView.get", "RFFI>F", dv_get);
  vm.registerNative("DataView.set", "RFFFI>V", dv_set);
  vm.registerNative("DataView.getBig", "RFII>R", dv_getBig);
  vm.registerNative("DataView.setBig", "RFRII>V", dv_setBig);
}

}  // namespace e7
