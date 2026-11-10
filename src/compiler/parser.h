#pragma once
#include "ast.h"

namespace e7 {
// Parses file.text into file.body. Returns false on syntax errors (appended to errs).
// Unsupported TypeScript that parses (index signatures, regular expressions, keyof ...) is reported in softErrs
// (when given) and parsed past, so the checker can report the rest of the file in the same compile.
bool parseSource(SourceFile& file, Arena& arena, std::vector<CompileError>& errs,
                 std::vector<CompileError>* softErrs = nullptr);
}  // namespace e7
