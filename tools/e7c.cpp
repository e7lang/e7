// e7c — E7 compiler command line.
#include <cstring>
#include <random>

#include "common.h"
#include "diag.h"
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include "compiler/compiler.h"
#include "core/module.h"
#include "core/since.h"
#include "e7/e7.h"

static int usage() {
  fprintf(stderr,
          "usage:\n"
          "  e7c <entry.ts|.e7> [-o out.e7b] [--debug] [--target <version>] [--sign <seed-hex-file>] [--disasm]\n"
          "      [--check] [--no-excerpt]\n"
          "      (default output: <entry>.e7b; with --disasm, a file is written only if -o is given)\n"
          "      --debug: include debug information (variable names, columns; see docs/DEBUGGING.md)\n"
          "      --target 0.2: the module must run on e7vm 0.2 (features of newer VMs are errors)\n"
          "  e7c --keygen <name>        writes <name>.seed (secret) and <name>.pub (public key, hex)\n"
          "  e7c --disasm-file <file.e7b>\n"
          "  e7c --info <file.e7b>       format, required e7vm version, signature, exports\n"
          "  e7c --version\n");
  return 2;
}

int main(int argc, char** argv) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif
  if (argc < 2) return usage();
  std::string input, output, signFile;
  bool disasm = false, checkOnly = false, excerpt = true, debugInfo = false;
  uint32_t target = 0;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--version" || a == "-v") { printf("e7c %s\n", E7_VERSION); return 0; }
    else if (a == "-o" && i + 1 < argc) output = argv[++i];
    else if (a == "--sign" && i + 1 < argc) signFile = argv[++i];
    else if (a == "--disasm") disasm = true;
    else if (a == "--check") checkOnly = true;
    else if (a == "--debug" || a == "-g") debugInfo = true;
    else if (a == "--target" && i + 1 < argc) {
      if (!e7::parseVersion(argv[++i], target)) { fprintf(stderr, "e7c: invalid --target %s (expected e.g. 0.2)\n", argv[i]); return 2; }
    }
    else if (a == "--no-excerpt") excerpt = false;
    else if (a == "--keygen" && i + 1 < argc) {
      std::string name = argv[++i];
      std::random_device rd;
      uint8_t seed[32], pk[32];
      for (auto& b : seed) b = (uint8_t)rd();
      e7_public_key(seed, pk);
      std::string s = toHex(seed, 32) + "\n", p = toHex(pk, 32) + "\n";
      if (!writeAll(name + ".seed", s.data(), s.size()) || !writeAll(name + ".pub", p.data(), p.size())) {
        fprintf(stderr, "e7c: cannot write key files\n");
        return 1;
      }
      printf("public key: %s", p.c_str());
      return 0;
    } else if (a == "--disasm-file" && i + 1 < argc) {
      std::string data;
      if (!readAll(argv[++i], data)) { fprintf(stderr, "e7c: cannot read %s\n", argv[i]); return 1; }
      size_t len = data.size();
      if (len > 76 && memcmp(data.data() + len - 76, "E7S1", 4) == 0) len -= 76;
      e7::Module m;
      std::string err;
      if (!e7::deserializeModule((const uint8_t*)data.data(), len, m, err)) { fprintf(stderr, "e7c: %s\n", err.c_str()); return 1; }
      printf("%s", e7::disassemble(m).c_str());
      return 0;
    } else if (a == "--info" && i + 1 < argc) {
      std::string data;
      if (!readAll(argv[++i], data)) { fprintf(stderr, "e7c: cannot read %s\n", argv[i]); return 1; }
      e7_module_info info;
      memset(&info, 0, sizeof info);
      info.struct_size = sizeof info;
      e7_status st = e7_inspect((const uint8_t*)data.data(), data.size(), &info);
      if (st == E7_ERR_LOAD || st == E7_ERR_ARGS) { fprintf(stderr, "e7c: %s is not a valid e7b module\n", argv[i]); return 1; }
      printf("format:   %u%s\n", info.format, info.format == e7::kFormatVersion ? "" : " (not supported by this e7vm)");
      if (info.format >= 2) printf("requires: e7vm %s\n", e7::versionString(info.requires_version).c_str());
      printf("debug:    %s\n", info.has_debug_info ? "yes" : "no");
      if (info.is_signed) printf("signed:   key id %s\n", toHex(info.key_id, 8).c_str());
      else printf("signed:   no\n");
      if (st == E7_OK) {
        size_t len = data.size() - (info.is_signed ? 76 : 0);
        e7::Module m;
        std::string err;
        if (e7::deserializeModule((const uint8_t*)data.data(), len, m, err)) {
          printf("exports:\n");
          for (auto& e : m.exports) printf("  %s(%s)\n", e.name.c_str(), e.hostSig.c_str());
        }
      }
      return st == E7_OK ? 0 : 1;
    } else if (a.size() && a[0] == '-') return usage();
    else input = a;
  }
  if (input.empty()) return usage();
  e7::CompileOptions copts;
  copts.debugInfo = debugInfo;
  copts.target = target;
  e7::CompileOutput co = e7::compileProgram(input, e7::readFileFromDisk, copts);
  if (!co.ok) {
    fprintf(stderr, "%s", formatCompileErrors(co, excerpt).c_str());
    return 1;
  }
  if (checkOnly) return 0;
  if (disasm) {
    printf("%s", e7::disassemble(co.module).c_str());
    // --disasm only inspects; write a module only when -o is given explicitly (so it never
    // overwrites a previously signed <name>.e7b with an unsigned build)
    if (output.empty()) return 0;
  }
  std::vector<uint8_t> bytes = e7::serializeModule(co.module);
  if (!signFile.empty()) {
    std::string hex;
    std::vector<uint8_t> seed;
    if (!readAll(signFile, hex) || !parseHex(hex, seed) || seed.size() != 32) {
      fprintf(stderr, "e7c: invalid signing key file %s (expects 64 hex chars)\n", signFile.c_str());
      return 1;
    }
    uint8_t* out;
    size_t outLen;
    if (e7_sign_module(bytes.data(), bytes.size(), seed.data(), &out, &outLen) != E7_OK) {
      fprintf(stderr, "e7c: signing failed\n");
      return 1;
    }
    bytes.assign(out, out + outLen);
    e7_free(out);
  }
  if (output.empty()) {
    output = input;
    size_t dot = output.find_last_of('.');
    if (dot != std::string::npos) output = output.substr(0, dot);
    output += ".e7b";
  }
  if (!writeAll(output, bytes.data(), bytes.size())) {
    fprintf(stderr, "e7c: cannot write %s\n", output.c_str());
    return 1;
  }
  return 0;
}
