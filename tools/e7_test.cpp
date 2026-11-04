// `e7 test`: discovers test files and runs every exported `test*` function in a fresh VM.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "common.h"
#include "diag.h"
#ifdef _WIN32
#include <io.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define E7_ISATTY(fd) _isatty(fd)
#else
#include <unistd.h>
#define E7_ISATTY(fd) isatty(fd)
#endif
#include "compiler/compiler.h"
#include "core/module.h"
#include "e7/e7.h"

namespace fs = std::filesystem;

namespace {

struct Options {
  std::vector<std::string> paths;
  std::string filter;
  bool json = false, verbose = false, excerpt = true;
  int64_t budget = 1000000000;
  size_t mem = 128u << 20;
  uint64_t seed = 0;
  bool hasSeed = false;
};

struct TestResult {
  std::string file, name;
  bool ok = false;
  double ms = 0;
  std::string error, output;
};

struct FileResult {
  std::string file;
  std::string error;  // compile error (no tests ran)
  size_t tests = 0;
};

int testUsage() {
  fprintf(stderr,
          "usage: e7 test [options] [files or directories...]\n"
          "  Runs every exported function named test* (no parameters) in each test file, each in a\n"
          "  fresh VM. A test passes when it returns normally (void, or true if it returns boolean).\n"
          "  Default: all **/*.test.ts and **/*.test.e7 files under the current directory\n"
          "  (node_modules, build* and hidden directories are skipped).\n"
          "  --filter <text>    only tests whose name (or file path) contains <text>\n"
          "  --budget <n>       instruction budget per test, incl. module initialization (default 1e9)\n"
          "  --mem <bytes>      heap limit per test (default 128 MiB)\n"
          "  --seed <n>         seed for Math.random\n"
          "  --json             machine-readable results on stdout\n"
          "  --verbose          also show the output of passing tests\n"
          "  --no-excerpt       compile errors without source excerpts\n");
  return 2;
}

bool isTestFile(const fs::path& p) {
  std::string n = p.filename().string();
  return endsWith(n, ".test.ts") || endsWith(n, ".test.e7");
}

bool skipDir(const fs::path& p) {
  std::string n = p.filename().string();
  return n == "node_modules" || n.rfind("build", 0) == 0 || (n.size() > 1 && n[0] == '.') || n == "out" ||
         n == "dist";
}

void collect(const fs::path& dir, std::vector<std::string>& out) {
  std::error_code ec;
  fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
  for (; !ec && it != end; it.increment(ec)) {
    const fs::path& p = it->path();
    std::error_code ec2;
    if (it->is_directory(ec2)) {
      if (!skipDir(p)) collect(p, out);
    } else if (isTestFile(p)) {
      out.push_back(p.generic_string());
    }
  }
}

std::string displayPath(const std::string& p) {
  std::string s = p;
  if (s.rfind("./", 0) == 0) s = s.substr(2);
  return s;
}

std::string jsonStr(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) {
          char b[8];
          snprintf(b, sizeof b, "\\u%04x", c);
          o += b;
        } else {
          o += (char)c;
        }
    }
  }
  return o + "\"";
}

std::string indent(const std::string& text, const std::string& pre) {
  std::string out;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t e = text.find('\n', pos);
    std::string l = text.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
    out += pre + l + "\n";
    if (e == std::string::npos) break;
    pos = e + 1;
  }
  return out;
}

// print hook: called once per printed line (without the newline)
void capturePrint(void* ud, const char* s, size_t n) {
  auto* out = static_cast<std::string*>(ud);
  if (out->size() < (1u << 20)) out->append(s, n).append("\n");
}

// Runs one test function in a fresh VM.
TestResult runOne(const Options& o, const std::string& file, const std::vector<uint8_t>& bytes, const std::string& name,
                  char ret) {
  TestResult r;
  r.file = file;
  r.name = name;
  e7_config cfg;
  e7_config_init(&cfg);
  cfg.memory_limit = o.mem;
  auto t0 = std::chrono::steady_clock::now();
  e7_vm* vm = e7_vm_new(&cfg);
  if (!vm) {
    r.error = "cannot create VM";
    return r;
  }
  e7_set_print(vm, capturePrint, &r.output);
  if (o.hasSeed) e7_seed_random(vm, o.seed);
  e7_set_budget(vm, o.budget);
  e7_module* mod = nullptr;
  e7_status st = e7_load(vm, bytes.data(), bytes.size(), &mod);
  if (st != E7_OK) {
    r.error = std::string("module initialization failed: ") + e7_last_error(vm);
  } else {
    e7_value v;
    st = e7_call(vm, mod, name.c_str(), nullptr, 0, &v);
    if (st != E7_OK) r.error = e7_last_error(vm);
    else if (ret == 'b' && !v.v.boolean) r.error = "test returned false";
    else r.ok = true;
  }
  e7_vm_free(vm);
  r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return r;
}

struct Style {
  bool color = false;
  std::string green(const std::string& s) const { return color ? "\x1b[32m" + s + "\x1b[0m" : s; }
  std::string red(const std::string& s) const { return color ? "\x1b[31m" + s + "\x1b[0m" : s; }
  std::string dim(const std::string& s) const { return color ? "\x1b[2m" + s + "\x1b[0m" : s; }
};

}  // namespace

int runTestCommand(int argc, char** argv) {
  Options o;
  for (int i = 0; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--filter" && i + 1 < argc) o.filter = argv[++i];
    else if (a == "--budget" && i + 1 < argc) o.budget = (int64_t)strtod(argv[++i], nullptr);
    else if (a == "--mem" && i + 1 < argc) o.mem = (size_t)strtod(argv[++i], nullptr);
    else if (a == "--seed" && i + 1 < argc) { o.seed = strtoull(argv[++i], nullptr, 10); o.hasSeed = true; }
    else if (a == "--json") o.json = true;
    else if (a == "--verbose") o.verbose = true;
    else if (a == "--no-excerpt") o.excerpt = false;
    else if (a == "--help" || a == "-h") { testUsage(); return 0; }
    else if (a.size() > 1 && a[0] == '-') return testUsage();
    else o.paths.push_back(a);
  }
  if (o.budget <= 0) o.budget = 1000000000;
#ifdef _WIN32
  if (!o.json && E7_ISATTY(_fileno(stdout))) SetConsoleOutputCP(CP_UTF8);
#endif
  Style sty;
  sty.color = !o.json && E7_ISATTY(1) && !getenv("NO_COLOR");

  // ---- discovery
  std::vector<std::string> files;
  if (o.paths.empty()) o.paths.push_back(".");
  for (const std::string& p : o.paths) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
      std::vector<std::string> found;
      collect(p, found);
      std::sort(found.begin(), found.end());
      files.insert(files.end(), found.begin(), found.end());
    } else if (fs::exists(p, ec)) {
      files.push_back(fs::path(p).generic_string());
    } else {
      fprintf(stderr, "e7 test: no such file or directory: %s\n", p.c_str());
      return 2;
    }
  }
  files.erase(std::unique(files.begin(), files.end()), files.end());
  if (files.empty()) {
    fprintf(stderr, "e7 test: no test files found (looking for *.test.ts / *.test.e7)\n");
    return 1;
  }

  auto t0 = std::chrono::steady_clock::now();
  std::vector<FileResult> fileResults;
  std::vector<TestResult> results;
  size_t passed = 0, failed = 0;
  const char* okMark = "\xE2\x9C\x93";    // check mark
  const char* failMark = "\xE2\x9C\x97";  // ballot x
  const char* sep = "\xE2\x80\xBA";       // single right-pointing angle quotation mark
  for (const std::string& f : files) {
    FileResult fr;
    fr.file = displayPath(f);
    e7::CompileOutput co = e7::compileProgram(f, e7::readFileFromDisk);
    if (!co.ok) {
      fr.error = formatCompileErrors(co, o.excerpt && !o.json);
      failed++;
      if (!o.json) {
        printf("%s %s %s\n", sty.red(failMark).c_str(), fr.file.c_str(), sty.red("(compile error)").c_str());
        printf("%s", indent(fr.error, "    ").c_str());
      }
      fileResults.push_back(fr);
      continue;
    }
    std::vector<uint8_t> bytes = e7::serializeModule(co.module);
    // run in source order (exports are listed by name): order by the first line of each function
    std::vector<e7::ExportDesc> exports = co.module.exports;
    auto firstLine = [&](const e7::ExportDesc& ex) -> uint32_t {
      const auto& L = co.module.funcs[ex.func].lines;
      return L.size() >= 2 && L[0] != e7::kLineExtMarker ? L[1] : 0;
    };
    std::stable_sort(exports.begin(), exports.end(),
                     [&](const e7::ExportDesc& a, const e7::ExportDesc& b) { return firstLine(a) < firstLine(b); });
    for (const e7::ExportDesc& ex : exports) {
      if (ex.name.rfind("test", 0) != 0) continue;
      bool nameMatch = o.filter.empty() || ex.name.find(o.filter) != std::string::npos ||
                       fr.file.find(o.filter) != std::string::npos;
      if (!nameMatch) continue;
      fr.tests++;
      TestResult r;
      size_t gt = ex.hostSig.find('>');
      char ret = ex.hostSig.empty() ? 'x' : ex.hostSig.back();
      if (gt != 0) {
        r.file = fr.file;
        r.name = ex.name;
        r.error = "test functions must not take parameters";
      } else if (ret != 'v' && ret != 'b') {
        r.file = fr.file;
        r.name = ex.name;
        r.error = "test functions must return void or boolean";
      } else {
        r = runOne(o, fr.file, bytes, ex.name, ret);
      }
      (r.ok ? passed : failed)++;
      if (!o.json) {
        char ms[32];
        snprintf(ms, sizeof ms, "(%.1f ms)", r.ms);
        printf("%s %s %s %s %s\n", r.ok ? sty.green(okMark).c_str() : sty.red(failMark).c_str(), fr.file.c_str(), sep,
               r.name.c_str(), sty.dim(ms).c_str());
        if (!r.ok) printf("%s", indent(r.error, "    ").c_str());
        if (!r.output.empty() && (!r.ok || o.verbose)) {
          printf("    %s\n", sty.dim("output:").c_str());
          printf("%s", indent(r.output, "    | ").c_str());
        }
      }
      results.push_back(r);
    }
    fileResults.push_back(fr);
  }
  double totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

  if (o.json) {
    std::string j = "{\"passed\":" + std::to_string(passed) + ",\"failed\":" + std::to_string(failed) + ",\"files\":[";
    for (size_t i = 0; i < fileResults.size(); i++) {
      const FileResult& fr = fileResults[i];
      j += std::string(i ? "," : "") + "{\"file\":" + jsonStr(fr.file) + ",\"tests\":" + std::to_string(fr.tests) +
           ",\"compileError\":" + (fr.error.empty() ? "null" : jsonStr(fr.error)) + "}";
    }
    j += "],\"tests\":[";
    for (size_t i = 0; i < results.size(); i++) {
      const TestResult& r = results[i];
      char ms[32];
      snprintf(ms, sizeof ms, "%.3f", r.ms);
      j += std::string(i ? "," : "") + "{\"file\":" + jsonStr(r.file) + ",\"name\":" + jsonStr(r.name) +
           ",\"ok\":" + (r.ok ? "true" : "false") + ",\"durationMs\":" + ms +
           ",\"error\":" + (r.ok ? "null" : jsonStr(r.error)) + ",\"output\":" + jsonStr(r.output) + "}";
    }
    j += "]}\n";
    printf("%s", j.c_str());
  } else {
    if (passed + failed == 0) printf("no tests matched\n");
    printf("\n%s, %s %s\n", sty.green(std::to_string(passed) + " passed").c_str(),
           (failed ? sty.red(std::to_string(failed) + " failed") : std::to_string(failed) + " failed").c_str(),
           sty.dim("(" + std::to_string(files.size()) + " file" + (files.size() == 1 ? "" : "s") + ", " +
                   std::to_string((long long)(totalMs + 0.5)) + " ms)")
               .c_str());
  }
  fflush(stdout);
  return failed ? 1 : (passed ? 0 : 1);
}
