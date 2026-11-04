# 生产就绪检查清单（v1.0）

本文件记录 e7vm 上线前的验证项与证据，便于复核。所有项目在 CI（`.github/workflows/ci.yml`）中持续执行。

## 1. 正确性

| 项目 | 方法 | 结果 |
|---|---|---|
| 语言/标准库语义 | `tests/run_tests.py`：72 个用例，与 Node.js 22 输出**逐字节**比对（差分测试）+ 预期输出 + 编译错误用例 | 全部通过 |
| TS 兼容性 | `tests/tsc/check.sh`：tsc 5.6 `--strict` 检查所有用例与示例 | 全部通过 |
| C API | `tests/capi_test.cpp`：参数编解码、宿主函数、KeyHandle、签名、预算挂起/恢复、内存/栈上限、错误后复用 | 通过 |
| 区域设置无关 | `tests/locale_test.cpp`（de_DE 下数字解析/格式化） | 通过 |
| 绑定 | JNI（OpenJDK 21 `E7VMTest`）、iOS xcframework 构建、Android NDK 构建、C#（Windows） | 通过 |

## 2. 平台与编译器矩阵（CI）

Linux GCC 13 / Clang 18 / Clang 16、32 位 i386、switch 分派、动态库、ASan+UBSan；macOS arm64 Release 与 ASan；
Windows MSVC 与 clang-cl；iOS；Android NDK；JNI。另在本地验证：Clang 15/17、libc++、aarch64（qemu）、MinGW（Wine）、
GCC UBSan（含 `float-cast-overflow`）。

**已知编译器缺陷及处理**：LLVM 15–17 的 CodeGenPrepare 地址模式下沉会错误编译 computed goto 解释器循环
（用 `-mllvm -opt-bisect-limit` 定位到 `CodeGen Prepare on e7::VM::execute`，再用 `-addr-sink-using-gep=false`
确认是该变换）。CMake 对这些编译器的 `interp.cpp` 关闭该变换；其他构建方式自动退回 switch 分派。见 README。

## 3. 安全（不可信输入）

| 威胁 | 防护 | 验证 |
|---|---|---|
| 篡改/伪造字节码 | Ed25519 签名（`require_signature` + `trusted_keys`），签名校验先于解析 | 错误公钥、未签名、单字节篡改均被拒绝（`e7 --trust`、capi_test） |
| 恶意字节码破坏内存 | 加载时字节码验证器（寄存器类型/范围、跳转目标、调用元数据、常量索引）+ 运行时对象形状检查 | `fuzz_load`：每次 CI 数千个变异模块，ASan/UBSan 下零崩溃 |
| 恶意源码（端上编译） | 解析嵌套上限 160、AST 深度上限 400、JSON 嵌套上限 256 | `fuzz_compile`：本轮累计约 6 万次变异，零内部错误、零崩溃 |
| 死循环 / 资源耗尽 | 指令预算（`e7_set_budget`）、`memory_limit`、`stack_slots`、`max_frames`、原生重入深度 64 | capi_test 覆盖每一项 |
| 密钥泄露 | `KeyHandle`：密钥只在宿主回调中出现，脚本不可读取/打印 | capi_test、示例 |
| 越权调用 | 脚本只能调用显式注册的宿主函数，加载时比对签名 | capi_test |

**宿主侧必须做的配置**（见 EMBEDDING.md §6）：开启签名校验、设置指令预算与内存上限。命令行工具 `e7` 仅用于开发，
默认不设预算。

## 4. 性能（x86-64，Release）

见 README 基准表；与 V8 `--jitless` 相当或更快。Clang 15–17 构建在规避措施下保持 computed goto（埃氏筛 ~330 ms，
GCC 290 ms，switch 回退 ~380 ms）。

## 5. 已知限制（非缺陷，按设计）

见 SPEC.md「不支持」列表：无 `try/catch`（`throw` 终止脚本）、无用户泛型、无正则、无 `any`；越界数组读取是运行时
错误；Date 为 UTC 语义；对象展开后可选属性的键顺序在编译期确定。
