#pragma once
#include "../core/module.h"
#include "checker.h"

namespace e7 {
// Generates a bytecode module from a checked program. Errors are appended to P.errors.
bool generateModule(Program& P, Checker& C, Module& out, const std::string& moduleName);
}  // namespace e7
