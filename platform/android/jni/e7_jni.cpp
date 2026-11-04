// JNI binding for org.e7vm.E7VM (Android NDK and desktop JVM). Uses only the public C API.
#include <jni.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "e7/e7.h"

namespace {

JavaVM* gJvm = nullptr;
jclass gExc, gDouble, gLong, gBoolean, gString, gNumber, gInteger, gShort, gByte, gFloat, gKeyHandle, gObject;
jmethodID gExcCtor, gDoubleOf, gLongOf, gBoolOf, gDoubleValue, gLongValue, gBoolValue;
jfieldID gKeyHandleId;

JNIEnv* env() {
  JNIEnv* e = nullptr;
  if (gJvm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) != JNI_OK) {
#ifdef __ANDROID__
    gJvm->AttachCurrentThread(&e, nullptr);
#else
    gJvm->AttachCurrentThread(reinterpret_cast<void**>(&e), nullptr);
#endif
  }
  return e;
}

std::string toUtf8(JNIEnv* e, jstring s) {
  if (!s) return {};
  const jchar* c = e->GetStringChars(s, nullptr);
  jsize n = e->GetStringLength(s);
  std::string out;
  out.reserve((size_t)n);
  for (jsize i = 0; i < n; i++) {
    uint32_t cp = c[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n && c[i + 1] >= 0xDC00 && c[i + 1] <= 0xDFFF) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (c[i + 1] - 0xDC00);
      i++;
    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
      cp = 0xFFFD;
    }
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
  }
  e->ReleaseStringChars(s, c);
  return out;
}

jstring fromUtf8(JNIEnv* e, const char* p, size_t n) {
  std::vector<jchar> out;
  out.reserve(n);
  size_t i = 0;
  const unsigned char* s = reinterpret_cast<const unsigned char*>(p);
  while (i < n) {
    uint32_t c = s[i], cp;
    int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (extra < 0 || i + extra >= n + (extra ? 0 : 1)) { out.push_back(0xFFFD); i++; continue; }
    cp = extra == 0 ? c : extra == 1 ? (c & 0x1F) : extra == 2 ? (c & 0x0F) : (c & 0x07);
    for (int k = 1; k <= extra; k++) cp = (cp << 6) | (s[i + k] & 0x3F);
    i += extra + 1;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back((jchar)(0xD800 + (cp >> 10)));
      out.push_back((jchar)(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back((jchar)cp);
    }
  }
  return e->NewString(out.data(), (jsize)out.size());
}

void throwE7(JNIEnv* e, int status, const char* msg) {
  jstring m = e->NewStringUTF(msg ? msg : "");
  jobject ex = e->NewObject(gExc, gExcCtor, (jint)status, m);
  e->Throw(static_cast<jthrowable>(ex));
}

struct State;
struct NativeRef {
  State* st;
  jobject fn;
  std::string sig;
  std::string retStr;
  std::vector<uint8_t> retBytes;
};

struct State {
  e7_vm* vm = nullptr;
  jobject keyProvider = nullptr;
  jobject printHandler = nullptr;
  std::vector<std::unique_ptr<NativeRef>> natives;
  std::vector<uint8_t> keyBuf;
  ~State() {
    JNIEnv* e = env();
    for (auto& n : natives) e->DeleteGlobalRef(n->fn);
    if (keyProvider) e->DeleteGlobalRef(keyProvider);
    if (printHandler) e->DeleteGlobalRef(printHandler);
    std::fill(keyBuf.begin(), keyBuf.end(), 0);
    e7_vm_free(vm);
  }
};

State* S(jlong h) { return reinterpret_cast<State*>(h); }

// Converts Java arguments; strings / bytes are kept alive in `store`.
bool toValues(JNIEnv* e, jobjectArray args, std::vector<e7_value>& out, std::vector<std::string>& store) {
  jsize n = args ? e->GetArrayLength(args) : 0;
  out.resize(n);
  store.resize(n);
  for (jsize i = 0; i < n; i++) {
    jobject a = e->GetObjectArrayElement(args, i);
    e7_value& v = out[i];
    memset(&v, 0, sizeof v);
    if (!a) v.type = E7_NULL;
    else if (e->IsInstanceOf(a, gDouble) || e->IsInstanceOf(a, gFloat)) {
      v.type = E7_NUMBER;
      v.v.num = e->CallDoubleMethod(a, gDoubleValue);
    } else if (e->IsInstanceOf(a, gLong) || e->IsInstanceOf(a, gInteger) || e->IsInstanceOf(a, gShort) || e->IsInstanceOf(a, gByte)) {
      v.type = E7_INT;
      v.v.i = e->CallLongMethod(a, gLongValue);
    } else if (e->IsInstanceOf(a, gBoolean)) {
      v.type = E7_BOOL;
      v.v.boolean = e->CallBooleanMethod(a, gBoolValue) ? 1 : 0;
    } else if (e->IsInstanceOf(a, gString)) {
      store[i] = toUtf8(e, static_cast<jstring>(a));
      v.type = E7_STRING;
      v.v.str.ptr = store[i].data();
      v.v.str.len = store[i].size();
    } else if (e->IsInstanceOf(a, gKeyHandle)) {
      v.type = E7_HANDLE;
      v.v.handle = e->GetLongField(a, gKeyHandleId);
    } else {
      jclass bc = e->FindClass("[B");
      bool isBytes = e->IsInstanceOf(a, bc);
      e->DeleteLocalRef(bc);
      if (!isBytes) {
        e->DeleteLocalRef(a);
        throwE7(e, E7_ERR_ARGS, "unsupported argument type");
        return false;
      }
      jbyteArray ba = static_cast<jbyteArray>(a);
      jsize len = e->GetArrayLength(ba);
      store[i].resize((size_t)len);
      if (len) e->GetByteArrayRegion(ba, 0, len, reinterpret_cast<jbyte*>(&store[i][0]));
      v.type = E7_BYTES;
      v.v.bytes.ptr = reinterpret_cast<const uint8_t*>(store[i].data());
      v.v.bytes.len = store[i].size();
    }
    e->DeleteLocalRef(a);
  }
  return true;
}

jobject toJava(JNIEnv* e, const e7_value& v) {
  switch (v.type) {
    case E7_NUMBER: return e->CallStaticObjectMethod(gDouble, gDoubleOf, (jdouble)v.v.num);
    case E7_INT: return e->CallStaticObjectMethod(gLong, gLongOf, (jlong)v.v.i);
    case E7_UINT: return e->CallStaticObjectMethod(gLong, gLongOf, (jlong)v.v.u);
    case E7_BOOL: return e->CallStaticObjectMethod(gBoolean, gBoolOf, (jboolean)(v.v.boolean != 0));
    case E7_STRING: return fromUtf8(e, v.v.str.ptr, v.v.str.len);
    case E7_BYTES: {
      jbyteArray b = e->NewByteArray((jsize)v.v.bytes.len);
      if (v.v.bytes.len) e->SetByteArrayRegion(b, 0, (jsize)v.v.bytes.len, reinterpret_cast<const jbyte*>(v.v.bytes.ptr));
      return b;
    }
    default: return nullptr;
  }
}

e7_status nativeThunk(e7_vm* vm, const e7_value* args, size_t n, e7_value* ret, void* user) {
  auto* ref = static_cast<NativeRef*>(user);
  JNIEnv* e = env();
  jobjectArray arr = e->NewObjectArray((jsize)n, gObject, nullptr);
  for (size_t i = 0; i < n; i++) {
    jobject o = args[i].type == E7_HANDLE ? nullptr : toJava(e, args[i]);
    if (args[i].type == E7_HANDLE) {
      jmethodID ctor = e->GetMethodID(gKeyHandle, "<init>", "(J)V");
      o = e->NewObject(gKeyHandle, ctor, (jlong)args[i].v.handle);
    }
    e->SetObjectArrayElement(arr, (jsize)i, o);
    if (o) e->DeleteLocalRef(o);
  }
  jclass fc = e->GetObjectClass(ref->fn);
  jmethodID inv = e->GetMethodID(fc, "invoke", "([Ljava/lang/Object;)Ljava/lang/Object;");
  e->DeleteLocalRef(fc);
  jobject r = e->CallObjectMethod(ref->fn, inv, arr);
  e->DeleteLocalRef(arr);
  if (e->ExceptionCheck()) {
    jthrowable t = e->ExceptionOccurred();
    e->ExceptionClear();
    jclass tc = e->GetObjectClass(t);
    jmethodID gm = e->GetMethodID(tc, "toString", "()Ljava/lang/String;");
    jstring m = static_cast<jstring>(e->CallObjectMethod(t, gm));
    std::string msg = toUtf8(e, m);
    e7_native_error(vm, msg.c_str());
    return E7_ERR_RUNTIME;
  }
  char rc = ref->sig.back();
  memset(ret, 0, sizeof *ret);
  if (rc == 'v') { if (r) e->DeleteLocalRef(r); return E7_OK; }
  if (!r) { e7_native_error(vm, "native returned null"); return E7_ERR_RUNTIME; }
  if (rc == 's') {
    ref->retStr = toUtf8(e, static_cast<jstring>(r));
    ret->type = E7_STRING;
    ret->v.str.ptr = ref->retStr.data();
    ret->v.str.len = ref->retStr.size();
  } else if (rc == 'y') {
    jbyteArray ba = static_cast<jbyteArray>(r);
    jsize len = e->GetArrayLength(ba);
    ref->retBytes.resize((size_t)len);
    if (len) e->GetByteArrayRegion(ba, 0, len, reinterpret_cast<jbyte*>(ref->retBytes.data()));
    ret->type = E7_BYTES;
    ret->v.bytes.ptr = ref->retBytes.data();
    ret->v.bytes.len = ref->retBytes.size();
  } else if (rc == 'b') {
    ret->type = E7_BOOL;
    ret->v.boolean = e->CallBooleanMethod(r, gBoolValue) ? 1 : 0;
  } else if (rc == 'n') {
    ret->type = E7_NUMBER;
    ret->v.num = e->CallDoubleMethod(r, gDoubleValue);
  } else {
    ret->type = E7_INT;
    ret->v.i = e->CallLongMethod(r, gLongValue);
  }
  e->DeleteLocalRef(r);
  return E7_OK;
}

int keyThunk(void* user, int64_t id, const uint8_t** key, size_t* len) {
  auto* st = static_cast<State*>(user);
  if (!st->keyProvider) return -1;
  JNIEnv* e = env();
  jclass c = e->GetObjectClass(st->keyProvider);
  jmethodID m = e->GetMethodID(c, "key", "(J)[B");
  e->DeleteLocalRef(c);
  jbyteArray b = static_cast<jbyteArray>(e->CallObjectMethod(st->keyProvider, m, (jlong)id));
  if (e->ExceptionCheck()) { e->ExceptionClear(); return -1; }
  if (!b) return -1;
  jsize n = e->GetArrayLength(b);
  std::fill(st->keyBuf.begin(), st->keyBuf.end(), 0);
  st->keyBuf.resize((size_t)n);
  if (n) e->GetByteArrayRegion(b, 0, n, reinterpret_cast<jbyte*>(st->keyBuf.data()));
  e->DeleteLocalRef(b);
  *key = st->keyBuf.data();
  *len = st->keyBuf.size();
  return 0;
}

void printThunk(void* user, const char* s, size_t n) {
  auto* st = static_cast<State*>(user);
  JNIEnv* e = env();
  if (!st->printHandler) return;
  jclass c = e->GetObjectClass(st->printHandler);
  jmethodID m = e->GetMethodID(c, "print", "(Ljava/lang/String;)V");
  e->DeleteLocalRef(c);
  jstring js = fromUtf8(e, s, n);
  e->CallVoidMethod(st->printHandler, m, js);
  e->DeleteLocalRef(js);
  if (e->ExceptionCheck()) e->ExceptionClear();
}

jobject finishCall(JNIEnv* e, State* st, e7_status s, const e7_value& ret) {
  if (s != E7_OK) {
    throwE7(e, s, s == E7_SUSPENDED ? "suspended" : e7_last_error(st->vm));
    return nullptr;
  }
  return toJava(e, ret);
}

jclass globalClass(JNIEnv* e, const char* n) {
  jclass c = e->FindClass(n);
  jclass g = static_cast<jclass>(e->NewGlobalRef(c));
  e->DeleteLocalRef(c);
  return g;
}

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  gJvm = vm;
  JNIEnv* e = env();
  gExc = globalClass(e, "org/e7vm/E7Exception");
  gDouble = globalClass(e, "java/lang/Double");
  gFloat = globalClass(e, "java/lang/Float");
  gLong = globalClass(e, "java/lang/Long");
  gInteger = globalClass(e, "java/lang/Integer");
  gShort = globalClass(e, "java/lang/Short");
  gByte = globalClass(e, "java/lang/Byte");
  gBoolean = globalClass(e, "java/lang/Boolean");
  gString = globalClass(e, "java/lang/String");
  gNumber = globalClass(e, "java/lang/Number");
  gObject = globalClass(e, "java/lang/Object");
  gKeyHandle = globalClass(e, "org/e7vm/KeyHandle");
  gExcCtor = e->GetMethodID(gExc, "<init>", "(ILjava/lang/String;)V");
  gDoubleOf = e->GetStaticMethodID(gDouble, "valueOf", "(D)Ljava/lang/Double;");
  gLongOf = e->GetStaticMethodID(gLong, "valueOf", "(J)Ljava/lang/Long;");
  gBoolOf = e->GetStaticMethodID(gBoolean, "valueOf", "(Z)Ljava/lang/Boolean;");
  gDoubleValue = e->GetMethodID(gNumber, "doubleValue", "()D");
  gLongValue = e->GetMethodID(gNumber, "longValue", "()J");
  gBoolValue = e->GetMethodID(gBoolean, "booleanValue", "()Z");
  gKeyHandleId = e->GetFieldID(gKeyHandle, "id", "J");
  return JNI_VERSION_1_6;
}

#define J(name) Java_org_e7vm_E7VM_##name

JNIEXPORT jlong JNICALL J(nativeNew)(JNIEnv* e, jclass, jlong mem, jint stack, jint frames, jboolean suspend, jbyteArray trusted) {
  e7_config cfg;
  e7_config_init(&cfg);
  if (mem > 0) cfg.memory_limit = (size_t)mem;
  if (stack > 0) cfg.stack_slots = (uint32_t)stack;
  if (frames > 0) cfg.max_frames = (uint32_t)frames;
  cfg.suspend_on_budget = suspend ? 1 : 0;
  std::vector<uint8_t> keys;
  if (trusted) {
    jsize n = e->GetArrayLength(trusted);
    keys.resize((size_t)n);
    if (n) e->GetByteArrayRegion(trusted, 0, n, reinterpret_cast<jbyte*>(keys.data()));
    cfg.require_signature = 1;
    cfg.trusted_keys = keys.data();
    cfg.num_trusted_keys = keys.size() / 32;
  }
  auto* st = new State();
  st->vm = e7_vm_new(&cfg);
  if (!st->vm) { delete st; return 0; }
  return reinterpret_cast<jlong>(st);
}

JNIEXPORT void JNICALL J(nativeFree)(JNIEnv*, jclass, jlong h) { delete S(h); }

JNIEXPORT jlong JNICALL J(nativeLoad)(JNIEnv* e, jclass, jlong h, jbyteArray code) {
  State* st = S(h);
  jsize n = e->GetArrayLength(code);
  std::vector<uint8_t> b((size_t)n);
  if (n) e->GetByteArrayRegion(code, 0, n, reinterpret_cast<jbyte*>(b.data()));
  e7_module* m = nullptr;
  e7_status s = e7_load(st->vm, b.data(), b.size(), &m);
  if (s != E7_OK) { throwE7(e, s, e7_last_error(st->vm)); return 0; }
  return reinterpret_cast<jlong>(m);
}

JNIEXPORT jlong JNICALL J(nativeLoadSource)(JNIEnv* e, jclass, jlong h, jbyteArray src, jstring name) {
  State* st = S(h);
  jsize n = e->GetArrayLength(src);
  std::string s((size_t)n, '\0');
  if (n) e->GetByteArrayRegion(src, 0, n, reinterpret_cast<jbyte*>(&s[0]));
  std::string nm = toUtf8(e, name);
  e7_module* m = nullptr;
  e7_status rc = e7_load_source(st->vm, s.data(), s.size(), nm.c_str(), &m);
  if (rc != E7_OK) { throwE7(e, rc, e7_last_error(st->vm)); return 0; }
  return reinterpret_cast<jlong>(m);
}

JNIEXPORT jobject JNICALL J(nativeCall)(JNIEnv* e, jclass, jlong h, jlong mod, jstring fn, jobjectArray args) {
  State* st = S(h);
  std::vector<e7_value> vals;
  std::vector<std::string> store;
  if (!toValues(e, args, vals, store)) return nullptr;
  std::string f = toUtf8(e, fn);
  e7_value ret;
  e7_status s = e7_call(st->vm, reinterpret_cast<e7_module*>(mod), f.c_str(), vals.data(), vals.size(), &ret);
  for (auto& x : store) std::fill(x.begin(), x.end(), '\0');
  return finishCall(e, st, s, ret);
}

JNIEXPORT jobject JNICALL J(nativeResume)(JNIEnv* e, jclass, jlong h) {
  State* st = S(h);
  e7_value ret;
  e7_status s = e7_resume(st->vm, &ret);
  return finishCall(e, st, s, ret);
}

JNIEXPORT void JNICALL J(nativeAbortSuspended)(JNIEnv*, jclass, jlong h) { e7_abort_suspended(S(h)->vm); }
JNIEXPORT void JNICALL J(nativeSetBudget)(JNIEnv*, jclass, jlong h, jlong t) { e7_set_budget(S(h)->vm, t); }
JNIEXPORT void JNICALL J(nativeSeed)(JNIEnv*, jclass, jlong h, jlong s) { e7_seed_random(S(h)->vm, (uint64_t)s); }
JNIEXPORT jlong JNICALL J(nativeMemoryUsed)(JNIEnv*, jclass, jlong h) { return (jlong)e7_memory_used(S(h)->vm); }

JNIEXPORT void JNICALL J(nativeRegister)(JNIEnv* e, jclass, jlong h, jstring name, jstring sig, jobject fn) {
  State* st = S(h);
  auto ref = std::make_unique<NativeRef>();
  ref->st = st;
  ref->fn = e->NewGlobalRef(fn);
  ref->sig = toUtf8(e, sig);
  std::string nm = toUtf8(e, name);
  e7_status s = e7_register_native(st->vm, nm.c_str(), ref->sig.c_str(), nativeThunk, ref.get());
  if (s != E7_OK) {
    e->DeleteGlobalRef(ref->fn);
    throwE7(e, s, e7_last_error(st->vm));
    return;
  }
  st->natives.push_back(std::move(ref));
}

JNIEXPORT void JNICALL J(nativeSetKeyProvider)(JNIEnv* e, jclass, jlong h, jobject p) {
  State* st = S(h);
  if (st->keyProvider) e->DeleteGlobalRef(st->keyProvider);
  st->keyProvider = p ? e->NewGlobalRef(p) : nullptr;
  e7_set_key_provider(st->vm, p ? keyThunk : nullptr, st);
}

JNIEXPORT void JNICALL J(nativeSetPrint)(JNIEnv* e, jclass, jlong h, jobject p) {
  State* st = S(h);
  if (st->printHandler) e->DeleteGlobalRef(st->printHandler);
  st->printHandler = p ? e->NewGlobalRef(p) : nullptr;
  e7_set_print(st->vm, p ? printThunk : nullptr, st);
}

JNIEXPORT jboolean JNICALL J(nativeHasFunction)(JNIEnv* e, jclass, jlong mod, jstring fn) {
  std::string f = toUtf8(e, fn);
  return e7_has_function(reinterpret_cast<e7_module*>(mod), f.c_str()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jbyteArray JNICALL J(nativeCompile)(JNIEnv* e, jclass, jbyteArray src, jstring name) {
  jsize n = e->GetArrayLength(src);
  std::string s((size_t)n, '\0');
  if (n) e->GetByteArrayRegion(src, 0, n, reinterpret_cast<jbyte*>(&s[0]));
  std::string nm = toUtf8(e, name);
  uint8_t* out = nullptr;
  size_t len = 0;
  char* errs = nullptr;
  e7_status st = e7_compile(s.data(), s.size(), nm.c_str(), &out, &len, &errs);
  if (st != E7_OK) {
    throwE7(e, st, errs ? errs : "compile error");
    e7_free(errs);
    return nullptr;
  }
  jbyteArray r = e->NewByteArray((jsize)len);
  e->SetByteArrayRegion(r, 0, (jsize)len, reinterpret_cast<const jbyte*>(out));
  e7_free(out);
  return r;
}

JNIEXPORT jbyteArray JNICALL J(nativeSign)(JNIEnv* e, jclass, jbyteArray code, jbyteArray seed) {
  if (e->GetArrayLength(seed) != 32) { throwE7(e, E7_ERR_ARGS, "seed must be 32 bytes"); return nullptr; }
  jsize n = e->GetArrayLength(code);
  std::vector<uint8_t> c((size_t)n);
  uint8_t sd[32];
  if (n) e->GetByteArrayRegion(code, 0, n, reinterpret_cast<jbyte*>(c.data()));
  e->GetByteArrayRegion(seed, 0, 32, reinterpret_cast<jbyte*>(sd));
  uint8_t* out = nullptr;
  size_t len = 0;
  e7_status st = e7_sign_module(c.data(), c.size(), sd, &out, &len);
  memset(sd, 0, sizeof sd);
  if (st != E7_OK) { throwE7(e, st, "signing failed"); return nullptr; }
  jbyteArray r = e->NewByteArray((jsize)len);
  e->SetByteArrayRegion(r, 0, (jsize)len, reinterpret_cast<const jbyte*>(out));
  e7_free(out);
  return r;
}

JNIEXPORT jbyteArray JNICALL J(nativePublicKey)(JNIEnv* e, jclass, jbyteArray seed) {
  if (e->GetArrayLength(seed) != 32) { throwE7(e, E7_ERR_ARGS, "seed must be 32 bytes"); return nullptr; }
  uint8_t sd[32], pk[32];
  e->GetByteArrayRegion(seed, 0, 32, reinterpret_cast<jbyte*>(sd));
  e7_public_key(sd, pk);
  memset(sd, 0, sizeof sd);
  jbyteArray r = e->NewByteArray(32);
  e->SetByteArrayRegion(r, 0, 32, reinterpret_cast<const jbyte*>(pk));
  return r;
}

}  // extern "C"
