// e7c — E7 compiler command line.
#include <cstring>
#include <random>

#include "common.h"
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include "compiler/compiler.h"
#include "core/module.h"
#include "e7/e7.h"

static int usage() {
  fprintf(stderr,
          "usage:\n"
          "  e7c <entry.ts|.e7> [-o out.e7b] [--sign <seed-hex-file>] [--disasm] [--check]\n"
          "  e7c --keygen <name>        writes <name>.seed (secret) and <name>.pub (public key, hex)\n"
          "  e7c --disasm-file <file.e7b>\n");
  return 2;
}

int main(int argc, char** argv) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif
  if (argc < 2) return usage();
  std::string input, output, signFile;
  bool disasm = false, checkOnly = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "-o" && i + 1 < argc) output = argv[++i];
    else if (a == "--sign" && i + 1 < argc) signFile = argv[++i];
    else if (a == "--disasm") disasm = true;
    else if (a == "--check") checkOnly = true;
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
    } else if (a.size() && a[0] == '-') return usage();
    else input = a;
  }
  if (input.empty()) return usage();
  e7::CompileOutput co = e7::compileProgram(input, e7::readFileFromDisk);
  if (!co.ok) {
    fprintf(stderr, "%s", co.errorText().c_str());
    return 1;
  }
  if (checkOnly) return 0;
  if (disasm) printf("%s", e7::disassemble(co.module).c_str());
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
