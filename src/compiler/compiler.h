// E7 compiler driver.
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "../core/module.h"
#include "ast.h"

namespace e7 {

using ReadFileFn = std::function<bool(const std::string& path, std::string& content)>;

struct CompileOutput {
  bool ok = false;
  Module module;
  std::vector<CompileError> errors;
  std::string errorText() const;  // "file:line:col: error: msg" lines
};

struct CompileOptions {
  bool debugInfo = false;  // emit debug information (see docs/DEBUGGING.md)
  uint32_t target = 0;     // oldest e7vm that must run the module (makeVersion); 0 = this compiler's VM
};

// Compiles the program rooted at entryPath. Imports ("./x") are resolved relative to the
// importing file and read through readFile (tries the path, then .ts and .e7 extensions).
CompileOutput compileProgram(const std::string& entryPath, const ReadFileFn& readFile,
                             const CompileOptions& opts = CompileOptions());

// Convenience: compile a single in-memory source (no imports of other files).
CompileOutput compileSource(const std::string& name, const std::string& source,
                            const CompileOptions& opts = CompileOptions());

// Default file reader (std::ifstream).
bool readFileFromDisk(const std::string& path, std::string& content);

}  // namespace e7
