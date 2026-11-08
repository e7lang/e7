// swift-tools-version:5.9
// Swift Package Manager manifest: builds e7vm (VM + compiler + BearSSL + Monocypher + fdlibm) from source.
//
//   dependencies: [.package(url: "https://github.com/e7lang/e7.git", from: "0.1.2")]
//   .target(name: "App", dependencies: [.product(name: "E7VM", package: "e7")])
//
// Keep the source lists in sync with E7_CORE_SRC / E7_VM_SRC / E7_COMPILER_SRC in CMakeLists.txt.
import PackageDescription

let coreSources = ["utf", "numfmt", "serialize", "verify", "disasm"].map { "src/core/\($0).cpp" }
  + ["third_party/fdlibm/ieee754.cpp"]
let vmSources = ["src/vm/vm.cpp", "src/vm/interp.cpp", "src/vm/debug.cpp", "src/vm/promise.cpp"]
  + ["core", "string", "array", "map", "math", "bytes", "bigint", "json", "encoding", "date", "test", "typed"].map { "src/stdlib/\($0).cpp" }
  + ["crypto", "sign"].map { "src/crypto/\($0).cpp" }
  + ["src/capi/capi.cpp"]
let compilerSources = [
  "parser", "types", "checker", "checker_stmt", "checker_expr", "checker_builtins", "checker_ns", "checker_generic", "codegen", "compiler",
].map { "src/compiler/\($0).cpp" }

let package = Package(
  name: "e7",
  platforms: [.iOS(.v13), .macOS(.v11)],
  products: [
    .library(name: "E7VM", targets: ["E7VM"])
  ],
  targets: [
    // C API (include/e7/e7.h) and the C++17 implementation; exposed to Swift as module `CE7VM`.
    .target(
      name: "CE7VM",
      path: ".",
      sources: coreSources + vmSources + compilerSources + [
        "third_party/bearssl/src",
        "third_party/monocypher/monocypher.c",
      ],
      publicHeadersPath: "include",
      cSettings: [
        .headerSearchPath("third_party/bearssl/inc"),
        .headerSearchPath("third_party/bearssl/src"),
        .headerSearchPath("third_party/monocypher"),
        // portable, UB-free byte access on every architecture (same as the CMake build)
        .define("BR_LE_UNALIGNED", to: "0"),
        .define("BR_BE_UNALIGNED", to: "0"),
      ],
      cxxSettings: [
        .headerSearchPath("src"),
        .headerSearchPath("third_party/bearssl/inc"),
        .headerSearchPath("third_party/monocypher"),
        .define("E7_WITH_COMPILER", to: "1"),
        // switch dispatch: the computed-goto loop needs a CMake-only per-file LLVM flag on Xcode 15/16
        .define("E7_NO_COMPUTED_GOTO", to: "1"),
      ]
    ),
    .target(
      name: "E7VM",
      dependencies: ["CE7VM"],
      path: "platform/ios/E7VM",
      sources: ["E7VM.swift", "E7ModuleManager.swift"]
    ),
    .testTarget(
      name: "E7VMTests",
      dependencies: ["E7VM", "CE7VM"],
      path: "Tests/E7VMTests"
    ),
  ],
  cLanguageStandard: .c99,
  cxxLanguageStandard: .cxx17
)
