# e7vm — E7 语言与虚拟机

**E7** 是一门 **严格静态类型的 TypeScript 子集**（每个合法的 E7 程序同时也是合法的 TypeScript 程序），
**e7vm** 是用 C++17 实现的嵌入式字节码虚拟机，面向 **Android / iOS / Windows / macOS / Linux**，
主要用于在 App 内安全、高效地执行**可由服务器下发的签名 / 验签等算法脚本**。

> 名字来源：`everettjf` = **e** + `verettj`（7 个字母）+ **f** → **e7f**，语言取名 **E7**。

```ts
// sign.ts —— 在 VS Code 里有完整的类型检查与补全（配合 lib/e7.d.ts）
import { hmac } from "e7:crypto";
import { hexEncode } from "e7:encoding";
import type { KeyHandle } from "e7:crypto";

export function sign(params: Map<string, string>, key: KeyHandle): string {
  const query = [...params.keys()].sort()
    .map(k => `${encodeURIComponent(k)}=${encodeURIComponent(params.get(k)!)}`).join("&");
  return hexEncode(hmac("sha256", key, new TextEncoder().encode(query)));
}
```

## 特性

- **语言**：严格 TS 子集 —— `let/const`、函数/箭头函数/闭包、类（单继承、`super`、静态成员、参数属性）、
  接口、类型别名、枚举、元组、解构、可选链 `?.`、`??`/`??=`、模板字符串、`for...of`、`switch`、标签语句、
  多文件模块 `import/export`、类型收窄（`=== null`、真值、`instanceof`、提前 return）。
- **严格静态类型**：没有 `any`；`T | null | undefined` 可空类型；编译期完成全部名字解析与类型检查。
- **数值语义与 JS 完全一致**：不标注时为 `number`（f64）；需要时显式使用定宽整数
  `i8/u8/i16/u16/i32/u32/i64/u64`（真正的机器整数，回绕溢出），以及 `rotl32/clz32/bswap32/mulhi64` 等单指令内建函数。
- **标准库**：`Math`、`Number`、`String`、`Array`（含 `map/filter/reduce/sort` 等）、`Map`、`Set`、`JSON`
  （`JSON.parse(text) as T` 会按类型**校验**）、`bigint`、`Uint8Array`/`ArrayBuffer`/`DataView`、
  `TextEncoder/Decoder`、`encodeURIComponent` 等，行为与 JavaScript 一致（与 Node.js 做差分测试）。
- **`e7:crypto`**（BearSSL + Monocypher，原生实现）：MD5/SHA-1/SHA-2/SHA-3/Keccak、HMAC、HKDF、PBKDF2、
  AES-GCM/CBC/CTR、ChaCha20-Poly1305、Ed25519、X25519、ECDSA（P-256/384/521）、RSA（PKCS#1 v1.5 / PSS）、
  CRC32、安全随机数；**KeyHandle** 让密钥只存在于宿主（Keychain/Keystore），脚本拿不到明文。
- **`e7:encoding`**：hex、base64、base64url、base32、UTF-8、PEM、字节拼接/比较。
- **字节码**：寄存器式 VM、64 位定长指令、类型化寄存器；`.e7b` 文件带 CRC32，可附加 **Ed25519 签名**。
- **安全**：加载时**字节码验证器**保证内存安全（即使字节码被篡改）；签名校验；指令预算、内存上限、调用深度上限；
  脚本只能调用宿主显式注册的函数；无 JIT（符合 iOS 要求）。
- **可挂起执行**：预算用完时可挂起，`e7_resume()` 继续 —— 长算法分片执行不阻塞 UI（为协程预留的基础）。
- **端上编译器**（可选）：手机上也可以直接编译源码；生产环境建议服务器编译 + 签名、端上只加载 `.e7b`。
- **绑定**：C API（`include/e7/e7.h`）、Java/Kotlin（JNI，已在 JVM 上测试）、Swift、C#。

## 快速开始

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/e7 examples/jwt.ts                    # 编译并运行
build/e7c examples/jwt.ts -o jwt.e7b        # 编译为字节码
build/e7c --keygen release                  # 生成 release.seed（私钥）和 release.pub
build/e7c app.ts -o app.e7b --sign release.seed
build/e7 --trust release.pub app.e7b        # 只接受该公钥签名的模块
build/e7c --disasm app.ts                   # 查看字节码
```

在宿主中嵌入（C）：

```c
#include "e7/e7.h"

e7_config cfg; e7_config_init(&cfg);
cfg.require_signature = 1; cfg.trusted_keys = server_pubkey; cfg.num_trusted_keys = 1;
e7_vm* vm = e7_vm_new(&cfg);
e7_set_key_provider(vm, my_keychain_lookup, NULL);
e7_module* m;
if (e7_load(vm, bytecode, len, &m) != E7_OK) { puts(e7_last_error(vm)); }
e7_value args[2] = { {.type = E7_STRING, .v.str = {"a=1&b=2", 7}}, {.type = E7_HANDLE, .v.handle = 1} };
e7_value ret;
e7_set_budget(vm, 10000000);                    // 防止死循环
if (e7_call(vm, m, "sign", args, 2, &ret) == E7_OK) printf("%.*s\n", (int)ret.v.str.len, ret.v.str.ptr);
e7_vm_free(vm);
```

更多见 [docs/EMBEDDING.md](docs/EMBEDDING.md)（Android/iOS/Windows 集成、宿主函数、密钥句柄、挂起/恢复）。

## 平台构建

| 平台 | 方式 |
|---|---|
| Linux / macOS | `cmake -S . -B build && cmake --build build && ctest --test-dir build` |
| Android | `ANDROID_NDK_HOME=... platform/android/build_android.sh` → `build-android/jniLibs/<abi>/libe7vm_jni.so`，Java/Kotlin API 在 `platform/java/org/e7vm` |
| iOS | `platform/ios/build_xcframework.sh` → `build-ios/e7vm.xcframework`，Swift 封装 `platform/ios/E7VM/E7VM.swift` |
| Windows | `platform/windows/build_windows.ps1 [-ClangCl] [-Shared]`（VS 2022）；C# 封装 `platform/windows/E7VM.cs` |

CMake 选项：`E7_WITH_COMPILER`（端上编译器，默认 ON）、`E7_BUILD_SHARED`、`E7_BUILD_JNI`、`E7_SANITIZE`、
`E7_NO_COMPUTED_GOTO`。动态库只导出 `e7_*` C API 符号。

## 测试

```bash
python3 tests/run_tests.py        # 语言/库测试：与 Node.js 输出逐字节比对（差分测试）+ 预期输出 + 错误用例
build/tests/capi_test             # C API：参数编解码、宿主函数、密钥句柄、签名、预算挂起、内存/栈上限
build/tests/fuzz_load 3000        # 字节码变异模糊测试：被篡改的模块必须被拒绝或安全执行，绝不崩溃
tests/tsc/check.sh                # 用官方 tsc（strict）验证所有测试与示例都是合法 TypeScript
```

已验证的配置：GCC 13 / Clang 15–18 / Apple Clang（macOS arm64）、Debug + ASan/UBSan、32 位（i386，对应 armeabi-v7a 的 32 位指针）、
switch 分派（MSVC 路径）、MinGW-w64 Windows 版（Wine 下全部通过）、动态库、JNI（OpenJDK 21）。
CI 配置见 `.github/workflows/ci.yml`（另含 macOS、iOS、Android NDK、MSVC、clang-cl）。

> 分派方式：GCC、Clang ≥ 18、Apple Clang ≥ 17 使用 computed goto；Clang 15–17（含 Xcode 15/16 的 Apple Clang、
> NDK r26 及更早）在 -O2/-O3 下会错误编译 computed goto 版本的解释器循环，因此自动改用 switch 分派（结果完全一致，
> 速度略慢）。也可用 `-DE7_NO_COMPUTED_GOTO=ON` 强制 switch。

## 性能

x86-64，Release，单位 ms（`bench/`）。与 Node.js 22 对比：V8 解释器（`--jitless`，与 e7vm 同为纯解释执行，
iOS 上也只能如此）和完整 JIT。

| 基准 | e7vm | V8 --jitless | V8 JIT |
|---|---:|---:|---:|
| fib(30) 递归调用 | 58 | 70 | 10 |
| 3000 万次循环（顶层全局变量） | 369 | 518 | 45 |
| 埃氏筛 500 万 | 298 | 465 | 91 |
| 400 万次对象分配 + 200 万次方法调用 | 395 | 739 | 38 |
| 纯脚本 SHA-256（u32）1 MiB | 168 | 1159 | 41 |
| 字符串拼接/拆分/Map | 384 | 293 | 243 |

签名类脚本的标准算法（HMAC、RSA、ECDSA 等）由原生代码执行，脚本只做参数组装，单次调用通常在亚毫秒级。

库体积（x86-64，`MinSizeRel`，剥离符号，含全部加密算法）：仅运行时约 470 KB，含端上编译器约 920 KB。

## 目录

```
include/e7/e7.h        C API
src/core/              字节码格式、序列化、验证器、反汇编、数字格式化（JS 兼容）
src/compiler/          词法/语法分析、类型检查、代码生成
src/vm/                解释器、对象模型、GC
src/stdlib/            标准库原生实现      src/crypto/   e7:crypto、Ed25519 模块签名、SHA-3
src/capi/              C API              tools/        e7、e7c 命令行
lib/e7.d.ts            IDE / tsc 类型声明  platform/     Android(JNI)/Java、iOS(Swift)、Windows(C#)
tests/ bench/ examples/ docs/          third_party/  BearSSL (MIT)、Monocypher (BSD-2/CC0)
```

## 文档

- [docs/SPEC.md](docs/SPEC.md) —— 语言规范（支持的语法、类型系统、数值语义、标准库、与 TS 的差异）
- [docs/EMBEDDING.md](docs/EMBEDDING.md) —— C API 与各平台集成、安全建议
- [docs/BYTECODE.md](docs/BYTECODE.md) —— `.e7b` 格式、指令集、验证规则、签名格式
- [docs/DESIGN.md](docs/DESIGN.md) —— 设计方案与决策记录

## 许可

本项目采用仓库根目录 LICENSE。第三方：BearSSL（MIT）、Monocypher（BSD-2-Clause / CC0），见 `third_party/`。
