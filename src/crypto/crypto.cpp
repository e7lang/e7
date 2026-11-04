// e7:crypto natives (placeholder; filled in below)
#include "../stdlib/util.h"

namespace e7 {
void registerEncodingNatives(VM& vm);
void registerCryptoNatives(VM& vm) { registerEncodingNatives(vm); }
}  // namespace e7
