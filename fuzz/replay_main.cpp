// Standalone driver for the libFuzzer targets: feeds each file given on the command line (directories
// are walked recursively) to LLVMFuzzerTestOneInput. Used to replay corpora and crash reproducers in
// regular CI (including the sanitizer jobs) without needing libFuzzer.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

static int runFile(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  LLVMFuzzerTestOneInput(buf.data(), buf.size());
  // also replay the input with the "repair checksum" flag flipped, for the loader target
  if (!buf.empty()) {
    buf.insert(buf.begin(), (uint8_t)1);
    LLVMFuzzerTestOneInput(buf.data(), buf.size());
  }
  return 1;
}

int main(int argc, char** argv) {
  int n = 0;
  for (int i = 1; i < argc; i++) {
    std::filesystem::path p(argv[i]);
    if (std::filesystem::is_directory(p)) {
      for (auto& e : std::filesystem::recursive_directory_iterator(p))
        if (e.is_regular_file()) n += runFile(e.path());
    } else if (std::filesystem::exists(p)) {
      n += runFile(p);
    } else {
      fprintf(stderr, "replay: no such file %s\n", argv[i]);
      return 2;
    }
  }
  printf("replayed %d inputs without crashes\n", n);
  return 0;
}
