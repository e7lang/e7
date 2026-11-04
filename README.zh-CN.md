# e7vm — E7 语言与虚拟机

[English](README.md) | **简体中文**

**E7** 是一门**严格静态类型的 TypeScript 子集**：每个合法的 E7 程序同时也是合法的 TypeScript 程序。
**e7vm** 是用 C++17 实现的轻量嵌入式字节码虚拟机，支持 **Android、iOS、Windows、macOS、Linux**，
用于在 App 内执行**由服务器下发的脚本**：主要是**签名 / 验签**，也包括常规的数学与字符串算法。
脚本运行在沙箱里，带签名，而且速度快。

```ts
// sign.ts：在 VS Code 里有完整的类型检查与补全（配合 lib/e7.d.ts）
import { hmac } from "e7:crypto";
import { hexEncode } from "e7:encoding";
import type { KeyHandle } from "e7:crypto";

export function sign(params: Map<string, string>, key: KeyHandle): string {
  const query = [...params.keys()].sort()
    .map(k => `${encodeURIComponent(k)}=${encodeURIComponent(params.get(k)!)}`).join("&");
  return hexEncode(hmac("sha256", key, new TextEncoder().encode(query)));
}
```

> 名字来源：`everettjf` = **e** + `verettj`（7 个字母）+ **f** → **e7f**，语言因此取名 **E7**。

## 为什么用 e7vm

- **算法更新不必发版。** 服务器编译并签名，下发 `.e7b` 字节码，App 校验后执行。
- **从设计上保证安全。**
  - Ed25519 模块签名。
  - 字节码验证器保证内存安全，字节码被篡改也一样。
  - 指令预算、内存上限、调用深度上限。
  - 无 JIT，符合 App Store 的要求。
  - 脚本只能调用宿主显式注册的函数。
- **密钥留在宿主。** 通过 `KeyHandle`，脚本可以使用 Keychain / Keystore 里的密钥，但拿不到密钥本身。
- **就是你熟悉的 TypeScript。** 语义与 JS 一致，能通过 `tsc --strict`，并与 Node.js 做差分测试。
- **体积小、速度快。**
  - 仅运行时约 470 KB，含端上编译器约 920 KB。
  - 解释器速度与 V8 的解释器（`--jitless`）相当或更快。
  - 标准密码算法由原生代码执行。

## 架构

<p align="center"><img src="docs/architecture.png" alt="e7vm architecture" width="760"></p>

| 层 | 位置 | 职责 |
|---|---|---|
| 编译器 | `src/compiler/` | 词法/语法分析、严格类型检查（类型收窄、闭包、类、模块）、带寄存器分配的代码生成 |
| 字节码 | `src/core/` | `.e7b` 格式（CRC32，可附 Ed25519 签名）、验证器、反汇编 |
| 虚拟机 | `src/vm/` | 寄存器式解释器（64 位指令、类型化寄存器、内联缓存）、精确标记清除 GC、预算/挂起 |
| 标准库 | `src/stdlib/`、`src/crypto/` | 与 JS 兼容的内建对象；加密与编码由原生代码实现 |
| 嵌入 | `src/capi/`、`platform/` | C API；Java/Kotlin（JNI）、Swift、C# 绑定 |

## 快速上手

### 1. 构建

需要 CMake ≥ 3.16 和支持 C++17 的编译器（GCC、Clang、MSVC）。Ninja 可选。

```bash
git clone https://github.com/everettjf/e7fvm.git && cd e7fvm
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

构建产物：`build/e7`（运行脚本）、`build/e7c`（编译、签名、反汇编）、`build/libe7vm_static.a`。

### 2. 编写并运行脚本

[`examples/embed/sign.ts`](examples/embed/sign.ts)：

```ts
import { hmac } from "e7:crypto";
import { hexEncode } from "e7:encoding";

// Called by the host app: returns hex(HMAC-SHA256(secret, message))
export function sign(message: string, secret: string): string {
  const enc = new TextEncoder();
  return hexEncode(hmac("sha256", enc.encode(secret), enc.encode(message)));
}

export function main(): string {
  return sign("amount=100&currency=CNY", "s3cr3t");
}
```

```bash
cd examples/embed
../../build/e7 sign.ts
# 0fa1ee36e152de30350b6a463f4f365d6574bea59f99ccfebb599c7b6b1f1fd6
```

`e7` 在内存中编译该文件，并调用导出的 `main`（或用 `--call` 指定的函数）。

### 3. 编译并签名，准备下发

```bash
../../build/e7c --keygen release                  # 生成 release.seed（私钥，保密）和 release.pub
../../build/e7c sign.ts -o sign.e7b --sign release.seed
../../build/e7 --trust release.pub sign.e7b       # 只运行该公钥签名的模块
../../build/e7c --disasm sign.ts                  # 可选：查看字节码
```

正式部署时，`release.seed` 只放在构建服务器（或 HSM）上，`release.pub` 编译进 App。

### 4. 在 App 中运行

[`examples/embed/host.c`](examples/embed/host.c)（节选）：

```c
#include "e7/e7.h"

e7_config cfg;
e7_config_init(&cfg);
cfg.require_signature = 1;            /* 只运行我们签名的模块 */
cfg.trusted_keys = pub;               /* 内置在 App 中的 32 字节 Ed25519 公钥 */
cfg.num_trusted_keys = 1;
cfg.memory_limit = 32u << 20;
e7_vm* vm = e7_vm_new(&cfg);

e7_module* mod;
if (e7_load(vm, code, len, &mod) != E7_OK) { puts(e7_last_error(vm)); return 1; }

e7_value args[2];
args[0].type = E7_STRING; args[0].v.str.ptr = "amount=100&currency=CNY"; args[0].v.str.len = 23;
args[1].type = E7_STRING; args[1].v.str.ptr = "s3cr3t";                  args[1].v.str.len = 6;
e7_value ret;
e7_set_budget(vm, 10000000);          /* 防止失控脚本 */
if (e7_call(vm, mod, "sign", args, 2, &ret) == E7_OK)
  printf("%.*s\n", (int)ret.v.str.len, ret.v.str.ptr);
e7_vm_free(vm);
```

```bash
cc -std=c11 host.c -I../../include ../../build/libe7vm_static.a \
   ../../build/libe7_bearssl.a ../../build/libe7_monocypher.a -lstdc++ -lm -o host
./host
# 0fa1ee36e152de30350b6a463f4f365d6574bea59f99ccfebb599c7b6b1f1fd6
```

Kotlin、Swift、C# 的流程相同：用受信公钥创建 VM，调用 `load(bytes)`，再 `call("sign", ...)`。
各平台的具体写法，以及宿主函数、`KeyHandle`、预算与分片执行，见 [docs/EMBEDDING.md](docs/EMBEDDING.md)。

### 5. 编辑器配置

把 [`lib/e7.d.ts`](lib/e7.d.ts) 加入 `tsconfig.json` 的 `files`，VS Code 里就有补全和类型报错。
对脚本运行 `tsc --strict`，得到的诊断与 `e7c` 一致。

## 特性

- **语言。**
  - 声明与函数：`let`/`const`、函数、箭头函数、闭包。
  - 类：单继承、`super`、静态成员、参数属性。
  - 类型：接口、类型别名、枚举、元组。
  - 语法：解构、对象展开、`?.`、`??`/`??=`、模板字符串、`for...of`、`switch`、标签语句。
  - 多文件 `import`/`export`，以及类型收窄。
- **严格类型。**
  - 没有 `any`；可空类型写作 `T | null | undefined`。
  - 编译期完成全部名字解析与类型检查。
- **数值语义与 JS 一致。**
  - `number` 即 64 位浮点数。
  - 可选的定宽整数 `i8 … u64`，溢出时回绕，以真正的机器整数执行。
  - `rotl32`、`clz32`、`mulhi64` 等单指令内建函数。
- **标准库。**
  - `Math`、`Number`、`String`、`Array`（`map`/`filter`/`reduce`/`sort` …）、`Map`、`Set`、`bigint`、`Date`（UTC）。
  - `JSON`：`JSON.parse(text) as T` 会按类型 `T` **校验**结果。
  - `Uint8Array`/`ArrayBuffer`/`DataView`、`TextEncoder`/`TextDecoder`、`encodeURIComponent`。
- **`e7:crypto`。**
  - 哈希：MD5、SHA-1、SHA-2、SHA-3、Keccak。
  - 密钥派生与消息认证：HMAC、HKDF、PBKDF2。
  - 加密：AES-GCM/CBC/CTR、ChaCha20-Poly1305。
  - 签名与密钥交换：Ed25519、X25519、ECDSA P-256/384/521（raw 或严格 DER）、RSA PKCS#1 v1.5 / PSS。
  - 另有 CRC32、安全随机数、`KeyHandle`。
- **`e7:encoding`。** hex、base64、base64url、base32、UTF-8、PEM，以及字节拼接与比较。
- **执行控制。**
  - 指令预算同时覆盖重型原生函数调用。
  - 可选的挂起/恢复（`e7_resume`），用于在 UI 线程上分片执行。
- **绑定。** C API（[`include/e7/e7.h`](include/e7/e7.h)）、Java/Kotlin（JNI）、Swift、C#。

完整的语言规范见 [docs/SPEC.md](docs/SPEC.md)。

## 平台

| 平台 | 构建 | 绑定 |
|---|---|---|
| Linux / macOS | `cmake -S . -B build && cmake --build build && ctest --test-dir build` | C/C++ |
| Android | `ANDROID_NDK_HOME=... platform/android/build_android.sh` → `build-android/jniLibs/<abi>/libe7vm_jni.so` | Java/Kotlin：`platform/java/org/e7vm` |
| iOS | `platform/ios/build_xcframework.sh` → `build-ios/e7vm.xcframework` | Swift：`platform/ios/E7VM/E7VM.swift` |
| Windows | `platform/windows/build_windows.ps1 [-ClangCl] [-Shared]`（VS 2022） | C/C++、C#：`platform/windows/E7VM.cs` |

CMake 选项：

| 选项 | 作用 |
|---|---|
| `E7_WITH_COMPILER` | 端上编译器（默认 ON） |
| `E7_BUILD_SHARED` | 构建动态库，只导出 `e7_*` C API |
| `E7_BUILD_JNI` | 构建 JNI 库 |
| `E7_SANITIZE` | 开启 ASan + UBSan |
| `E7_NO_COMPUTED_GOTO` | 使用 switch 分派 |

## 安全与生产就绪

e7vm 就是为运行从网络下发的代码而设计的。发布前经过了 4 轮独立安全审查、与 Node.js/OpenSSL 的差分测试、
sanitizer 和模糊测试，所有发现的问题都已修复并配有回归测试。证据见 [docs/PRODUCTION.md](docs/PRODUCTION.md)。
宿主方必须：

1. **开启签名校验**（`require_signature` + 内置在 App 里的公钥），私钥只放在构建服务器上。
2. **设置指令预算和内存上限**（例如 16–64 MiB）。
3. **用 `KeyHandle` 传递密钥**，不要以字符串或字节参数的形式传给脚本。

## 测试

```bash
python3 tests/run_tests.py     # 语言/库测试：与 Node.js 输出逐字节比对，外加错误用例
ctest --test-dir build         # 全部测试套件：C API、重入、恶意模块、模糊测试、区域设置
tests/tsc/check.sh             # 所有测试和示例都能通过 tsc --strict
```

每次推送，CI（[`.github/workflows/ci.yml`](.github/workflows/ci.yml)）运行 15 个任务：

- **Linux：** GCC、Clang 18、Clang 16、ASan+UBSan、switch 分派、动态库、32 位。
- **macOS：** Release 与 ASan。
- **移动端：** iOS、Android NDK、JNI。
- **Windows：** MSVC 与 clang-cl。
- **TypeScript：** `tsc` 严格检查。

## 性能

数据来自 x86-64 的 Release 构建，单位为毫秒（脚本在 `bench/`）。对比对象是 Node.js 22，包括 V8 解释器
（`--jitless`：与 e7vm 一样是纯解释执行，也是 iOS 上唯一的选择）和完整 JIT。

| 基准 | e7vm | V8 --jitless | V8 JIT |
|---|---:|---:|---:|
| fib(30) 递归调用 | 58 | 70 | 10 |
| 3000 万次循环（顶层全局变量） | 369 | 518 | 45 |
| 埃氏筛 500 万 | 298 | 465 | 91 |
| 400 万次对象分配 + 200 万次方法调用 | 395 | 739 | 38 |
| 纯脚本 SHA-256（u32）1 MiB | 168 | 1159 | 41 |
| 字符串拼接/拆分/Map | 384 | 293 | 243 |

在签名类脚本中，标准算法（HMAC、RSA、ECDSA 等）由原生代码执行，单次调用通常远低于 1 毫秒。

## 目录结构

```
include/e7/e7.h     C API                      lib/e7.d.ts      IDE / tsc 类型声明
src/compiler/       语法分析、类型检查、代码生成
src/core/           字节码格式、序列化、验证器、反汇编、JS 数字格式化
src/vm/             解释器、对象模型、GC
src/stdlib/         内建库                     src/crypto/      e7:crypto、模块签名
src/capi/           C API                      tools/           e7、e7c 命令行工具
platform/           Android（JNI）/ Java、iOS（Swift）、Windows（C#）
examples/           示例脚本 + 嵌入示例（examples/embed）
tests/ bench/ docs/                            third_party/     BearSSL（MIT）、Monocypher（BSD-2/CC0）
```

## 文档

以下文档均为英文：

- [docs/SPEC.md](docs/SPEC.md)：语言规范（语法、类型系统、数值语义、标准库、与 TS 的差异）
- [docs/EMBEDDING.md](docs/EMBEDDING.md)：C API、Android/iOS/Windows 集成、安全建议
- [docs/BYTECODE.md](docs/BYTECODE.md)：`.e7b` 格式、指令集、验证规则、签名格式
- [docs/DESIGN.md](docs/DESIGN.md)：设计方案与决策记录
- [docs/PRODUCTION.md](docs/PRODUCTION.md)：生产就绪检查清单（验证项与证据）

## 许可

见 [LICENSE](LICENSE)。第三方代码：BearSSL（MIT）、Monocypher（BSD-2-Clause / CC0），位于 `third_party/`。
