// Human-readable compiler diagnostics for the command line tools: each error keeps its first line
// ("file:line:col: error: msg", which tools parse) and is followed by the source line and a caret
// under the error column.
#pragma once
#include <map>
#include <string>

#include "common.h"
#include "compiler/compiler.h"

static inline std::string sourceExcerpt(const std::string& text, uint32_t line, uint32_t col) {
  if (line == 0) return "";
  size_t pos = 0;
  for (uint32_t l = 1; l < line; l++) {
    pos = text.find('\n', pos);
    if (pos == std::string::npos) return "";
    pos++;
  }
  size_t end = text.find('\n', pos);
  std::string src = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
  if (!src.empty() && src.back() == '\r') src.pop_back();
  if (src.size() > 240) return "";  // minified / generated code: the excerpt would not help
  std::string num = std::to_string(line);
  std::string gutter(num.size(), ' ');
  std::string out = " " + num + " | " + src + "\n";
  if (col >= 1 && col - 1 <= src.size()) {
    std::string pad;
    for (size_t i = 0; i + 1 < col && i < src.size(); i++) {
      unsigned char c = (unsigned char)src[i];
      if (c == '\t') pad += '\t';
      else if ((c & 0xC0) != 0x80) pad += ' ';  // one column per UTF-8 character
    }
    out += " " + gutter + " | " + pad + "^\n";
  }
  return out;
}

// errorText() plus a source excerpt under each error.
static inline std::string formatCompileErrors(const e7::CompileOutput& co, bool excerpt) {
  if (!excerpt) return co.errorText();
  std::map<std::string, std::string> sources;
  std::string out;
  for (auto& e : co.errors) {
    out += e.file + ":" + std::to_string(e.loc.line) + ":" + std::to_string(e.loc.col) + ": error: " + e.msg + "\n";
    auto it = sources.find(e.file);
    if (it == sources.end()) {
      std::string text;
      if (!readAll(e.file, text)) text.clear();
      it = sources.emplace(e.file, text).first;
    }
    out += sourceExcerpt(it->second, e.loc.line, e.loc.col);
  }
  return out;
}
