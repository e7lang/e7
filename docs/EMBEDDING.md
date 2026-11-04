# 嵌入指南：C API 与 Android / iOS / Windows 集成

## 1. 推荐的部署流程（脚本由服务器下发）

```
开发：VS Code 写 .ts（lib/e7.d.ts 提供类型）→ e7 本地运行 / Node 对照测试
构建服务器：e7c app.ts -o app.e7b --sign release.seed      （私钥只在服务器）
下发：CDN / 接口返回 app.e7b（可再套一层 TLS）
App：e7_config.trusted_keys = release.pub → e7_load() 校验签名 + 字节码验证 → e7_call()
```

- 生成密钥：`e7c --keygen release` 得到 `release.seed`（32 字节私钥种子，十六进制，**保密**）与 `release.pub`（公钥，内置到 App）。
- 支持多把公钥（密钥轮换）：`trusted_keys` 传 N×32 字节。
- `.e7b` 带格式版本号；VM 拒绝不认识的版本。服务器可按客户端 VM 版本下发对应产物。
- 端上也可直接编译源码（`e7_compile` / `e7_load_source`，需 `E7_WITH_COMPILER=ON`）；
  只需要运行 `.e7b` 时可用 `-DE7_WITH_COMPILER=OFF` 减小体积。

## 2. C API 速览（`include/e7/e7.h`）

| 函数 | 说明 |
|---|---|
| `e7_config_init` / `e7_vm_new` / `e7_vm_free` | 创建/销毁 VM。配置：`memory_limit`、`stack_slots`、`max_frames`、`suspend_on_budget`、`require_signature`、`trusted_keys` |
| `e7_load(vm, data, len, &module)` | 验签 → 解析 → 验证 → 链接原生函数 → 运行模块顶层代码 |
| `e7_compile` / `e7_load_source` | 编译源码（错误信息为 `文件:行:列: error: ...`） |
| `e7_call(vm, module, "fn", args, n, &ret)` | 调用 `export function` |
| `e7_set_budget(vm, ticks)` | 后续执行的指令预算（≤0 不限） |
| `e7_resume` / `e7_abort_suspended` | 继续 / 放弃被挂起的调用 |
| `e7_register_native(vm, name, sig, fn, user)` | 注册宿主函数（须在 `e7_load` 前） |
| `e7_set_key_provider` | 为 `KeyHandle` 提供密钥 |
| `e7_set_print` | 接管 `console.log` 输出 |
| `e7_seed_random` | 固定 `Math.random` 种子 |
| `e7_sign_module` / `e7_public_key` | Ed25519 签名工具（一般在服务器端用） |
| `e7_last_error` | 最近一次错误信息（含脚本调用栈） |

**值类型** `e7_value`：`E7_NUMBER`(double)、`E7_BOOL`、`E7_INT`/`E7_UINT`(64 位)、`E7_STRING`(UTF-8 指针+长度)、
`E7_BYTES`、`E7_HANDLE`、`E7_VOID`、`E7_NULL`。返回的字符串/字节由 VM 持有，**在下一次调用该 VM 之前有效**。

**签名码**（宿主函数与导出函数）：`n` number、`b` boolean、`s` string、`y` Uint8Array、`h` KeyHandle、`v` void、
`1..8` = `i8 u8 i16 u16 i32 u32 i64 u64`。例：`"sy>s"` 表示 `(string, Uint8Array) => string`。

**错误码**：`E7_OK`、`E7_SUSPENDED`、`E7_ERR_RUNTIME`（脚本错误/throw/abort）、`E7_ERR_BUDGET`、`E7_ERR_MEMORY`、
`E7_ERR_STACK`、`E7_ERR_LOAD`、`E7_ERR_VERIFY`、`E7_ERR_SIGNATURE`、`E7_ERR_NOT_FOUND`、`E7_ERR_ARGS`、`E7_ERR_COMPILE`、`E7_ERR_STATE`。
任何错误之后 VM 都可以继续使用；模块全局变量保留。

### 2.1 宿主函数

```ts
// 脚本侧
declare function deviceId(): string;
declare function hostLog(level: number, msg: string): void;
```
```c
static e7_status device_id(e7_vm* vm, const e7_value* a, size_t n, e7_value* ret, void* user) {
  ret->type = E7_STRING; ret->v.str.ptr = g_device_id; ret->v.str.len = strlen(g_device_id);
  return E7_OK;            // 出错：e7_native_error(vm, "msg"); return E7_ERR_RUNTIME;
}
e7_register_native(vm, "deviceId", ">s", device_id, NULL);
```
加载模块时会比对脚本声明与宿主注册的签名；缺失或不一致都会使 `e7_load` 返回 `E7_ERR_LOAD`。
脚本**只能**调用这些显式注册的函数（以及内置标准库），没有文件、网络或系统访问能力。

### 2.2 密钥句柄（密钥不进入脚本）

```ts
import type { KeyHandle } from "e7:crypto";
export function sign(k: KeyHandle, data: string): string { /* hmac("sha256", k, ...) */ }
```
```c
static int key_lookup(void* user, int64_t id, const uint8_t** key, size_t* len) {
  /* 从 Keychain / Keystore / DPAPI 解密出密钥，放到宿主管理的缓冲区 */
  *key = buf; *len = buf_len; return 0;     // 未知 id 返回非 0
}
e7_set_key_provider(vm, key_lookup, NULL);
e7_value args[2] = {{.type = E7_HANDLE, .v.handle = 1}, ...};
```
脚本拿到的只是不可伪造的句柄对象；`hmac` 在原生代码中直接使用宿主提供的字节，脚本无法读出、打印或外传密钥。

### 2.3 指令预算与分片执行

```c
e7_config cfg; e7_config_init(&cfg); cfg.suspend_on_budget = 1;
e7_vm* vm = e7_vm_new(&cfg);
e7_set_budget(vm, 200000);                       // 每片约 1 ms 量级
e7_status st = e7_call(vm, m, "longTask", args, n, &ret);
while (st == E7_SUSPENDED) {                     // 例如每帧继续一片，UI 不卡顿
  e7_set_budget(vm, 200000);
  st = e7_resume(vm, &ret);
}
```
不开启挂起时，预算耗尽返回 `E7_ERR_BUDGET`，用于防止死循环。字符串/数组/编码/JSON/哈希/BigInt 等原生函数按处理的数据量计入预算
（调用结束后结算），因此预算同时限制了脚本调用重型原生函数的总耗时。挂起期间不能发起新的 `e7_call`（`E7_ERR_STATE`），
可用 `e7_abort_suspended` 放弃。

### 2.4 线程

一个 VM 只能在一个线程中使用；不同 VM 之间没有共享状态，可以在不同线程并行。需要并发时每个线程创建自己的 VM
（加载同一份 `.e7b` 即可）。

## 3. Android（Java / Kotlin）

构建：`ANDROID_NDK_HOME=... platform/android/build_android.sh`，把 `build-android/jniLibs/*` 复制到
`app/src/main/jniLibs/`，把 `platform/java/org/e7vm/*.java` 加入工程（Kotlin 可直接使用）。

```kotlin
import org.e7vm.*

val vm = E7VM(E7VM.Config().requireSignature(SERVER_PUBLIC_KEY).memoryLimit(32L shl 20))
vm.setKeyProvider { id -> keyStoreDecrypt(id) }          // 返回 ByteArray
vm.registerNative("deviceId", ">s") { _ -> Settings.Secure.getString(cr, ANDROID_ID) }
val module = vm.load(bytesFromServer)                      // 签名/验证失败抛 E7Exception
vm.setBudget(10_000_000)
val sig = vm.call(module, "sign", "a=1&b=2", KeyHandle(1)) as String
vm.close()
```
参数：`Double/Float` → number，`Int/Long` → 整数，`Boolean`，`String`，`ByteArray` → Uint8Array，`KeyHandle`。
返回：`Double`、`Long`、`Boolean`、`String`、`ByteArray` 或 `null`。错误为 `E7Exception(status, message)`。
JNI 绑定已在桌面 JVM（OpenJDK 21）上通过 `platform/java/test/E7VMTest.java` 测试。

## 4. iOS（Swift）

构建：`platform/ios/build_xcframework.sh` → `build-ios/e7vm.xcframework`；将其与 `platform/ios/E7VM/E7VM.swift`
加入 App target。

```swift
let vm = try E7VM(memoryLimit: 32 << 20, trustedKeys: [serverPublicKey])
vm.keyProvider = { id in Keychain.key(for: id) }
try vm.registerNative("deviceId", signature: ">s") { _ in .string(UIDevice.current.identifierForVendor!.uuidString) }
let module = try vm.load(bytecode)
vm.setBudget(10_000_000)
if case let .string(sig) = try vm.call(module, "sign", .string("a=1&b=2"), .handle(KeyHandle(1))) { print(sig) }
```
e7vm 不使用 JIT、不申请可执行内存，符合 App Store 对解释执行的要求（下发脚本不应改变 App 的主要用途）。

## 5. Windows（C / C++ / C#）

- C/C++：`platform/windows/build_windows.ps1`（VS 2022；`-ClangCl` 启用 computed goto 分派，`-Shared` 生成 `e7vm.dll`），
  链接 `e7vm_static.lib`（及 `e7_bearssl.lib`、`e7_monocypher.lib`、`advapi32.lib`）或 `e7vm.dll`。
- C#：`platform/windows/E7VM.cs`（需 `e7vm.dll`，x64）。

```csharp
using var vm = new E7.E7VM(memoryLimit: 32 << 20, suspendOnBudget: false, serverPublicKey);
vm.SetKeyProvider(id => ProtectedData.Unprotect(LoadBlob(id), null, DataProtectionScope.CurrentUser));
var module = vm.Load(File.ReadAllBytes("app.e7b"));
var sig = (string)vm.Call(module, "sign", "a=1&b=2", new E7.KeyHandle(1));
```

## 6. 安全建议

1. **始终开启签名校验**（`require_signature` + 内置公钥）；私钥只放在构建服务器或 HSM。
2. 设置 `memory_limit`（如 16–64 MiB）和指令预算；对外部输入驱动的脚本设置较小的 `max_frames`。
3. 密钥通过 `KeyHandle` 提供，不要以字符串/字节参数传给脚本。
4. 只注册必要的宿主函数，并在宿主侧校验其参数。
5. 字节码验证器 + 运行时检查保证被篡改的字节码不能破坏内存；但"算法逻辑本身"在客户端无法做到绝对保密，
   需要保密的密钥应留在宿主或服务器。
6. 脚本错误信息包含源文件名和行号；如需隐藏，可在服务器端用不同文件名编译。

## 7. 体积与性能参考

- Linux x86-64 动态库（`MinSizeRel`、剥离符号、含全部加密算法）：仅运行时约 **470 KB**，含端上编译器约 **920 KB**。
- 解释器性能与 V8 的解释器（`--jitless`）相当或更快；标准密码算法由原生代码执行。详见 README 的基准表。
