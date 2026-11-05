// Test-only access to the VM behind an e7_vm (used by tests/; not installed and not part of the
// C API). The VM's testing hooks are listed in src/vm/vm.h ("testing hooks").
#pragma once
#include "e7/e7.h"

namespace e7 {
class VM;
}
e7::VM* e7_testing_vm(e7_vm* v);
