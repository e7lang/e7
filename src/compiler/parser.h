#pragma once
#include "ast.h"

namespace e7 {
// Parses file.text into file.body. Returns false on syntax errors (appended to errs).
bool parseSource(SourceFile& file, Arena& arena, std::vector<CompileError>& errs);
}  // namespace e7
