#include "compiler.h"
#include "../core/since.h"

#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_map>

#include "checker.h"
#include "codegen.h"
#include "parser.h"

namespace e7 {

std::string CompileOutput::errorText() const {
  std::string out;
  for (auto& e : errors) {
    out += e.file + ":" + std::to_string(e.loc.line) + ":" + std::to_string(e.loc.col) + ": error: " + e.msg + "\n";
  }
  return out;
}

bool readFileFromDisk(const std::string& path, std::string& content) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  content = ss.str();
  return true;
}

static std::string normalizePath(const std::string& p) {
  std::vector<std::string> parts;
  std::string cur;
  bool abs = !p.empty() && p[0] == '/';
  for (size_t i = 0; i <= p.size(); i++) {
    if (i == p.size() || p[i] == '/' || p[i] == '\\') {
      if (cur == "..") {
        if (!parts.empty() && parts.back() != "..") parts.pop_back();
        else if (!abs) parts.push_back("..");
      } else if (!cur.empty() && cur != ".") {
        parts.push_back(cur);
      }
      cur.clear();
    } else {
      cur.push_back(p[i]);
    }
  }
  std::string out = abs ? "/" : "";
  for (size_t i = 0; i < parts.size(); i++) out += (i ? "/" : "") + parts[i];
  return out.empty() ? "." : out;
}

static std::string dirOf(const std::string& p) {
  size_t s = p.find_last_of("/\\");
  return s == std::string::npos ? "" : p.substr(0, s);
}

namespace {

struct Loader {
  Program& P;
  const ReadFileFn& read;
  std::unordered_map<std::string, SourceFile*> byPath;
  std::set<std::string> visiting;
  std::vector<CompileError>& errs;

  std::string resolvePath(const std::string& spec, const std::string& from, std::string* content) {
    if (spec.empty() || (spec[0] != '.' && spec[0] != '/')) return "";
    std::string base = normalizePath((spec[0] == '/' ? "" : (dirOf(from).empty() ? "" : dirOf(from) + "/")) + spec);
    const char* exts[] = {"", ".ts", ".e7", "/index.ts", "/index.e7"};
    for (const char* ext : exts) {
      std::string cand = base + ext;
      if (byPath.count(cand)) return cand;
      std::string c;
      if (read(cand, c)) {
        if (content) *content = c;
        return cand;
      }
    }
    // strip .js extension used by TS projects
    if (base.size() > 3 && base.compare(base.size() - 3, 3, ".js") == 0) {
      std::string b2 = base.substr(0, base.size() - 3);
      for (const char* ext : {".ts", ".e7"}) {
        std::string c;
        std::string cand = b2 + ext;
        if (byPath.count(cand)) return cand;
        if (read(cand, c)) {
          if (content) *content = c;
          return cand;
        }
      }
    }
    return "";
  }

  SourceFile* load(const std::string& path, const std::string& text) {
    auto it = byPath.find(path);
    if (it != byPath.end()) return it->second;
    if (visiting.count(path)) {
      errs.push_back(CompileError{path, Loc{1, 1}, "circular import involving '" + path + "'"});
      return nullptr;
    }
    visiting.insert(path);
    auto sf = std::make_unique<SourceFile>();
    sf->path = path;
    sf->text = text;
    if (!parseSource(*sf, P.arena, errs)) {
      visiting.erase(path);
      return nullptr;
    }
    // load dependencies first, in source order (imports, side-effect imports and re-exports): the files
    // are initialized in this order, as JavaScript evaluates modules (depth first, each once)
    for (Stmt* s : sf->body) {
      if (s->k != SK::Import && !(s->k == SK::Export && !s->from.empty())) continue;
      if (s->from.rfind("e7:", 0) == 0) continue;
      std::string content;
      std::string dep = resolvePath(s->from, path, &content);
      if (dep.empty()) {
        errs.push_back(CompileError{path, s->loc, "cannot find module '" + s->from + "'"});
        continue;
      }
      if (byPath.count(dep)) continue;
      load(dep, content);
    }
    visiting.erase(path);
    SourceFile* raw = sf.get();
    P.files.push_back(std::move(sf));
    byPath[path] = raw;
    return raw;
  }
};

}  // namespace

CompileOutput compileProgram(const std::string& entryPath, const ReadFileFn& readFile, const CompileOptions& opts) {
  CompileOutput out;
  Program P;
  std::string entry = normalizePath(entryPath);
  std::string text;
  if (!readFile(entry, text)) {
    out.errors.push_back(CompileError{entry, Loc{0, 0}, "cannot read file"});
    return out;
  }
  Loader L{P, readFile, {}, {}, out.errors};
  L.load(entry, text);
  if (!out.errors.empty()) return out;
  // modules reached only through `import type` / `export type ... from` are erased by TypeScript and never run:
  // they are checked (their types are used) but not initialized
  {
    for (auto& f : P.files) f->evaluated = false;
    std::vector<SourceFile*> work;
    auto it0 = L.byPath.find(entry);
    if (it0 != L.byPath.end()) { it0->second->evaluated = true; work.push_back(it0->second); }
    while (!work.empty()) {
      SourceFile* f = work.back();
      work.pop_back();
      for (Stmt* st : f->body) {
        if (st->typeOnly || st->from.empty() || st->from.rfind("e7:", 0) == 0) continue;
        if (st->k != SK::Import && st->k != SK::Export) continue;
        auto it = L.byPath.find(L.resolvePath(st->from, f->path, nullptr));
        if (it == L.byPath.end() || it->second->evaluated) continue;
        it->second->evaluated = true;
        work.push_back(it->second);
      }
    }
  }
  Checker C(P, [&](const std::string& spec, const std::string& from) -> SourceFile* {
    std::string p = L.resolvePath(spec, from, nullptr);
    auto it = L.byPath.find(p);
    return it == L.byPath.end() ? nullptr : it->second;
  });
  C.run();
  if (!P.errors.empty()) {
    out.errors = P.errors;
    return out;
  }
  std::string modName = entry;
  size_t sl = modName.find_last_of('/');
  if (sl != std::string::npos) modName = modName.substr(sl + 1);
  if (!generateModule(P, C, out.module, modName, opts.debugInfo)) {
    out.errors = P.errors;
    return out;
  }
  out.module.minVm = moduleRequires(out.module);
  std::string verr;
  if (!verifyModule(out.module, verr)) {
    out.errors.push_back(CompileError{entry, Loc{0, 0}, "internal compiler error: generated bytecode failed verification: " + verr});
    return out;
  }
  out.ok = true;
  return out;
}

CompileOutput compileSource(const std::string& name, const std::string& source, const CompileOptions& opts) {
  std::string n = normalizePath(name);
  return compileProgram(n, [&](const std::string& path, std::string& content) {
    if (path == n) { content = source; return true; }
    return false;
  }, opts);
}

}  // namespace e7
