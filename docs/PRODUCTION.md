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

### 3.1 独立安全审查（字节码加载路径）

由一个独立的审查代理（只拿到源码和"任意字节序列不得破坏内存"的目标，不知道我们的结论）针对反序列化、验证器、
加载/链接、解释器以及标准库原生函数做了定向审查，并用手工构造的恶意 `.e7b` 在 ASan 下复现。发现并已修复 4 个问题：

| # | 问题 | 修复 |
|---|---|---|
| 1 | 导出函数的宿主签名（`hostSig`）未校验，C API 会按伪造的类型转换参数/返回值（类型混淆、空签名 UB） | 验证器要求 hostSig 格式正确且与函数寄存器类型逐一一致 |
| 2 | 内联缓存槽被不同类型的指令（GETN_I/F/R、SETN_*、CALLI）共用时，命中路径会按错误类型读字段或越界读 vtable | 验证器要求每个 IC 槽只服务一种用途；CALLI 命中时再做 vtable 边界检查 |
| 3 | `concatBytes`、`Map.fromEntries`、`console.log` 假设数组元素是引用 | 检查数组元素类型 / 按实际类型格式化 |
| 4 | `Map.new/Set.new` 的键模式与键类型可不一致（把 double 当指针哈希） | 要求键模式与键类型的寄存器种类一致 |

另外顺带修复：若干原生函数在范围检查前就把字节码给出的 double 转成整数（UB），统一改为先检查（`immIndex`）。
6 个恶意样本保存在 `tests/malicious/`，由 `malicious_test`（C API，CI 中含 ASan）回归测试。审查确认正确的部分：
反序列化边界与整数溢出、寄存器/常量/索引范围、跳转目标（不能跳入参数字）、调用参数与元数据、闭包捕获、
类继承布局、运行时类型检查、栈/帧上限、GC 根。

### 3.2 第二轮独立审查（标准库/加密原生函数、GC、C API）

| # | 问题 | 修复 |
|---|---|---|
| 1 | `rsaSign`：私钥中的 n 与 p·q 不一致时 BearSSL 栈缓冲区越界**写**（可由普通 TS + 恶意密钥触发） | 解析时校验 n == p·q、512 ≤ bits ≤ 4096；PKCS#8 嵌套深度上限 |
| 2 | `e7_call` 转换宿主参数时可能触发 GC，回收已转换的参数（UAF） | 参数转换期间暂停 GC（`VM::GCPause`） |
| 3/4 | 宿主函数内重入 `e7_call/e7_resume/e7_load`，或调用 `e7_collect_garbage`/`e7_abort_suspended` 破坏执行状态 | C API 运行中拒绝重入（`E7_ERR_STATE`），回收/中止调用在运行中被忽略 |
| 5 | 循环/超深数组 `String(x)`/`join` 无限递归 | 循环引用按 JS 输出空串，嵌套深度上限 256，输出大小受内存上限约束 |
| 6/7 | `JSON.parse` 类型描述符（来自字节码）递归无界、越界读、`?~` 组合泄漏指针 | 描述符先做严格语法校验（边界、深度、包装层规则） |
| 8 | BigInt 大小不计入内存上限；超大 BigInt 的二次算法无视指令预算 | 上限 262144 位，limb 计入堆；按块解析；`**` 精确判断位数 |
| 9 | `replace` 的 `$\``/`$'` 二次膨胀；子串搜索 O(n·m)；PBKDF2 迭代不计预算 | 临时缓冲受内存上限约束（`checkTemp`）；KMP 搜索 O(n+m)；PBKDF2 按 HMAC 次数预先扣预算 |
| 其他 | `lastIndexOf`/`toExponential` 的 double→int UB；数值数组原生函数未检查元素类型（信息泄漏） | 先检查范围；检查 `vt` |

回归测试：`tests/capi_reentry_test.cpp`、`tests/malicious/`（新增 JSON 描述符与循环 join 样本）、
`tests/cases/errors/rt_rsa_bad_key.ts`、`rt_bigint_size.ts`、`rt_replace_oom.ts`、`tests/cases/lib/string_search.ts`
（与 Node 的随机差分测试）。另用 660 个"边界数值 × 内建函数"脚本（±Infinity/NaN/±1e300/-0/±2^53…）在
float-cast-overflow UBSan 下运行，零报告。

### 3.3 第三轮独立审查（加密正确性与编码，含与 Node 的差分对比）

| # | 问题 | 修复 |
|---|---|---|
| 1 | `rsaVerify` 公钥解析对嵌套 SPKI 无限递归（栈溢出） | 只允许一层 SPKI，检查 rsaEncryption OID，拒绝尾随数据 |
| 2 | **合法**的 DER ECDSA 签名在 r/s 都短于曲线长度时被拒（P-521 约 26%） | 自写严格 DER 解码并左补齐；2×长度的输入先按 raw、再按 DER 尝试 |
| 3 | BearSSL 宽松 ASN.1 导致同一 (r,s) 的多种非 DER 编码都能验证（可塑性） | 严格 DER：最小长度、非负且最小整数编码、无尾随数据 |
| 4 | RSA 超长公钥指数 e / 超长 dp、dq 使 modpow 绕过指令预算（可达数小时） | e ≤ 64 位（同 OpenSSL）；dp/dq/iq 长度不超过 p/q |
| 5 | `ecdsaPublicKey` 接受 0 或 ≥ n 的私钥标量 | 统一检查 1 ≤ x < n |
| 6 | Ed25519 验签（余因子方程）对攻击者给定的小阶公钥接受任意消息的零签名 | 拒绝非规范编码与 8 阶子群点（编码独立计算，与 libsodium 一致） |
| 7 | X25519 未拒绝低阶公钥（全零共享密钥） | 常量时间检查全零输出并报错（RFC 7748 §6.1） |

已确认正确：RSA PKCS#1 v1.5 完整 DigestInfo 比对、PSS 各边界、ECDSA r/s 为 0 或 ≥ n 拒绝、GCM/ChaCha 常量时间
认证、CBC 填充、CTR 计数器回绕、HKDF/PBKDF2/HMAC/全部哈希、各编码与 Node 一致、KeyHandle 不泄漏、随机数来源、
模块签名覆盖范围。回归测试：`crypto_ecdsa_der.ts`（OpenSSL 生成的 72 组 DER/raw 签名）、`crypto_ecdsa_strict_der.ts`、
`crypto_ed25519_strict.ts`、`rt_x25519_low_order.ts`、`rt_ec_scalar_zero.ts`、`rt_rsa_huge_e.ts`。

**宿主侧必须做的配置**（见 EMBEDDING.md §6）：开启签名校验、设置指令预算与内存上限。命令行工具 `e7` 仅用于开发，
默认不设预算。

## 4. 性能（x86-64，Release）

见 README 基准表；与 V8 `--jitless` 相当或更快。Clang 15–17 构建在规避措施下保持 computed goto（埃氏筛 ~330 ms，
GCC 290 ms，switch 回退 ~380 ms）。

## 5. 已知限制（非缺陷，按设计）

见 SPEC.md「不支持」列表：无 `try/catch`（`throw` 终止脚本）、无用户泛型、无正则、无 `any`；越界数组读取是运行时
错误；Date 为 UTC 语义；对象展开后可选属性的键顺序在编译期确定。
