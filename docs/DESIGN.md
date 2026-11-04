# E7 语言与 e7vm 虚拟机设计方案（草案 v0.4）

> v0.4 变更：明确主要负载是 **签名 / 验签算法**。新增 §3.8（面向签名场景的设计）：定宽整数类型与位运算、字节处理、原生 `e7:crypto` 模块、`bigint`、密钥句柄；**建议修订**整数字面量默认类型（见 §3.2）；安全章节新增可选的字节码加固。
>
> v0.3 变更：定名 **E7 / e7vm**；语法**只采用严格 TypeScript 子集**（取消 Ruby 方案）；整数字面量默认 `int`；**不支持异常**（`try/catch/throw`）；负载定为常规数学/数值算法 + 字符串处理，据此规划标准库与字符串实现。
>
> v0.2 变更：根据反馈改为 **严格静态类型**、语法以 **TypeScript 严格子集** 为主；明确 **脚本由服务器下发**、**端上带编译器（可选）**、**为协程预留设计**；新增命名调研。
> 现阶段只做设计，不开发。

已确认的需求：

| 项 | 结论 |
|---|---|
| 类型 | 严格静态类型，无 `any` |
| 语法 | **严格 TypeScript 子集**（每个 E7 程序都是合法 TS） |
| 命名 | 语言 **E7**，引擎 **e7vm** |
| 数值 | 整数字面量默认 `int`(i64)，`number` = f64 |
| 异常 | **不支持** `try/catch/throw`；运行时错误直接终止脚本并返回宿主 |
| 负载 | **以签名/验签算法为主**，兼顾常规数学算法与字符串处理 |
| 脚本来源 | **服务器下发** → 签名校验 + 字节码验证 + 沙箱是 P0 |
| 编译器 | 核心是 VM 跑 `.e7b`；编译器也要能在手机上跑（可选模块） |
| 协程 | v1 不提供语法，但 VM 架构从第一天按“可挂起”设计 |
| 平台 | Android / iOS / Windows，C++17 |

---

## 1. 命名调研

### 1.1 一个发现：`e7f` 就是 `everettjf` 的缩写
`everettjf` = **e** + `verettj`（7 个字母）+ **f** → **e7f**，与 `i18n`（internationalization）、`k8s`（kubernetes）是同一种数字缩写法（numeronym）。
语言名 **E7** 即取自 e7f，引擎名定为更简洁的 **e7vm**（检索未发现同名项目）。

### 1.2 候选名

| 名字 | 与 everettjf 的关系 | 冲突情况（检索结果） | 评价 |
|---|---|---|---|
| **E7** ⭐ | e7f 缩写的前半部分 | 未发现同名编程语言；只有 Berkeley 的 E7 课程、Esterel v7 | 短、好记、扩展名自然（`.e7` / `.e7b`）；推荐 |
| **E7F** | 完整 numeronym | 未发现冲突 | 最独特，但读起来略拗口；适合作为项目/组织名 |
| E8 | 无直接关系（E7 的“下一代”；数学上 E8 李群很有美感） | 有小众 `e8vm` 项目和 PSU 课程语言 E8 | 失去与 ID 的关联 |
| Eve | Everett 前三字母 | 有 Eve 语言（witheve，2018 年停更），另有 EVE Online | 冲突较多，不推荐 |
| Evr / Ever | Everett 的 "ever" | 未发现同名语言 | 寓意好（ever），可作备选 |
| Rett | Everett 的后半 | 与医学名词 Rett 综合征同名 | 不推荐 |
| Jet | j(e)t / jf | JetBrains、Microsoft Jet 引擎等大量冲突 | 不推荐 |

注：“E” 本身已经是一门老语言（1997 年 Electric Communities 的 E），所以用 “E7” 而不是 “E”。

### 1.3 命名体系（已确定）
- 语言：**E7**（读作 “E-seven”，来自 e7f = everettjf）
- 引擎：**e7vm**（库 `libe7vm`、可选编译器库 `libe7c`）
- 仓库：当前为 `e7fvm`，可在 GitHub 设置中改名为 `e7vm`（GitHub 会自动重定向旧地址）
- 源文件 `.e7`，字节码 `.e7b`，编译器命令 `e7c`，运行器 `e7`
- 版本演进可以自然地留给 **E8**（例如引入协程/泛型大版本时）

---

## 2. 语法方案调研

### 2.1 可参考的“静态类型脚本”先例

| 项目 | 做法 | 对我们的启示 |
|---|---|---|
| **Static TypeScript（微软 MakeCode，MPLR 2019 论文）** | TS 的语法子集，类改为**名义类型**，编译到 ARM 机器码，mark-sweep GC；对象访问密集的基准比 MicroPython/Duktape 快 ≥20 倍，比 V8 慢不到 2 倍 | **最直接的先例**：证明“严格 TS 子集 + 静态编译”可行且很快 |
| AssemblyScript | TS 语法 + `i32/i64/f64` 等数值类型（通过声明文件让 tsc 认识），编译到 Wasm | 用 `.d.ts` 声明额外数值类型，**IDE 白拿** |
| Swift / Kotlin | 现代静态类型语言的表达力标杆 | 借鉴其可空类型 `T?`、表达式风格 |
| Wasm / JVM | 加载时做字节码类型验证 | 服务器下发字节码的安全模型 |

### 2.2 E7 = 严格 TypeScript 子集

核心规则：**每一个合法的 E7 程序，同时也是合法的 TypeScript 程序**（反之不成立：E7 更严格）。

同一个算法的写法：

```ts
// 快速排序
function quicksort(a: number[], lo = 0, hi = a.length - 1): void {
  if (lo >= hi) return;
  const p = a[(lo + hi) >> 1];
  let i = lo, j = hi;
  while (i <= j) {
    while (a[i] < p) i++;
    while (a[j] > p) j--;
    if (i <= j) { [a[i], a[j]] = [a[j], a[i]]; i++; j--; }
  }
  quicksort(a, lo, j);
  quicksort(a, i, hi);
}

class Point {
  constructor(public x: number, public y: number) {}   // 一行声明字段 + 构造
  dist(o: Point): number { return Math.hypot(this.x - o.x, this.y - o.y); }
}

const pts = [new Point(0, 0), new Point(3, 4)];
const total = pts.map(p => p.x + p.y).reduce((s, v) => s + v, 0);  // 推断为 number
export function main(): string { return `total=${total}`; }
```

选它的理由：
1. **工具链白拿**：VS Code / WebStorm 直接提供高亮、补全、跳转、类型报错（配一个 `e7.d.ts` 声明标准库和 `int` 等类型即可）。手机端开发者、前端开发者几乎零学习成本。
2. **可以做差分测试**：同一个脚本在 Node.js 和 E7 VM 上各跑一遍、比对输出，这是免费又强力的正确性测试手段。
3. **静态类型天然存在**：TS 的类型标注语法成熟（泛型、可空、联合、函数类型），不用自己设计。
4. 优雅度靠：类型推断、箭头函数、解构、模板字符串、`for...of`、构造函数参数属性、可选链 `?.`、`??`。

代价：TS 的结构化类型、`any`、原型链等必须裁剪掉（见 §3）。

### 2.3 结论
- **只采用严格 TS 子集**，不再考虑 Ruby 风格语法。简洁性靠类型推断和精选语法特性保证。
- 编译器内部仍保持“前端（语法 → 类型化 AST）/ 后端（类型化 IR → 字节码）”分层，这是常规工程实践，便于测试和优化。

---

## 3. E7 语言规范要点（v1）

### 3.1 类型系统
| 类别 | 内容 | 说明 |
|---|---|---|
| 基本类型 | `boolean` `number` `string` `bigint` | 与 TS 语义一致；`number` = f64 |
| 定宽整数（E7 扩展） | `i8 u8 i16 u16 i32 u32 i64 u64`，`int` = `i64` 的别名 | 真正的机器整数：回绕溢出、按位宽做位运算（见 §3.8.1）。在 `e7.d.ts` 中声明为 `number`/`bigint` 的别名，IDE 照常工作 |
| 字节 | `Uint8Array`、`DataView` | 字节串的标准表示，签名数据的输入输出都用它 |
| 数组 | `T[]`、`Float64Array`、`Int32Array` … | 静态类型下 `number[]` **本身就是连续 f64 存储**，无装箱 |
| 容器 | `Map<K,V>` `Set<T>` | 内置泛型 |
| 类 | `class`（单继承、`implements`） | **名义类型**，字段布局编译期固定，方法走 vtable |
| 接口 | `interface` | 编译期结构检查，运行时用 itable 分派 |
| 记录 | `type P = { x: number, y: number }` / 对象字面量 | 编译为匿名定长结构体 |
| 可空 | `T \| null`、`?.`、`??`、`!` | 只有 `null`，**没有 `undefined`** |
| 枚举 | `enum`、字符串字面量联合 `"a" \| "b"` | 编译为整数 |
| 函数 | 函数类型、箭头函数、闭包、默认参数、元组返回 `[int, string]` | |
| 泛型 | 泛型函数和类 | 按“值类别”单态化（各整数宽度/number/引用各生成一份） |
| 模块 | `import` / `export` | 编译期链接 |

### 3.2 数值语义（v0.4 已确认）
v0.3 定的是“整数字面量默认 `int`(i64)”。在明确负载是签名算法之后，改为：

- **不写类型时，完全遵循 TS/JS 语义**：整数字面量是 `number`(f64)；`& | ^ << >> >>>` 按 JS 规则先转 int32 再运算；`Math.imul`、`x >>> 0`、`(a + b) | 0` 等写法结果与 Node 完全一致。
- **标注了定宽整数类型时，采用机器整数语义**：`let h: u32 = 0x6a09e667`，加减乘回绕、位运算按 32 位、`>>` 对有符号是算术右移、对无符号是逻辑右移。

修订理由：
1. 现有大量 JS/TS 签名代码（哈希、HMAC、自定义混淆算法）大量依赖 `|0`、`>>>0` 这类 JS 位运算语义。如果整数字面量默认是 i64，这类代码在 E7 里**能编译通过但结果悄悄变了**（例如 JS 中 `1 << 31` 是负数，i64 下是正数；`x | 0` 在 i64 下不会截断）。对签名算法来说这是最危险的一类 bug。
2. 修订后：从 JS 移植过来的代码不改也能得到相同结果；需要性能时，把关键变量标注成 `u32`/`u64` 即可走快速路径。
3. 性能影响很小：解释器里 f64 加减与整数加减开销相当，瓶颈在指令分派；真正需要快的位运算代码会显式用定宽类型。

其它规则不变：
- 定宽整数与 `number` 之间必须显式转换（`u32(x)`、`Number(h)`），不同宽度之间也必须显式转换，杜绝隐式截断。
- 整数除零是运行时错误；`number` 除零按 IEEE 得到 Infinity/NaN（与 JS 一致）。
- 不开 fast-math，保证三端浮点结果一致（确定性）。

### 3.3 刻意不支持（相对 TS）
`any` / `unknown` 的随意转换、`undefined`、`==`（只允许 `===`）、`var`、原型链/`prototype`、`eval`/`Function()`、运行时增删属性、`delete`、**`try`/`catch`/`finally`/`throw`**、getter/setter（v1）、装饰器、`with`、宽松的结构化赋值转换、声明合并。

### 3.4 错误处理（无异常）
- 可预期的失败用返回值表达：`T | null`，或元组 `[T, string | null]`。
- 运行时错误（数组越界、空引用、整数除零、预算/内存超限、栈溢出）以及脚本调用 `abort(msg)`：**立即终止本次调用**，`e7_call` 返回错误码，`e7_last_error` 给出消息和带源码行号的调用栈。VM 状态保持可用，宿主可以继续调用其它函数。
- 好处：VM 不需要异常展开表；生成的字节码和验证器都更简单。

### 3.5 预留关键字（为协程准备）
`function*` / `yield`、`async` / `await` 在 v1 解析器中**识别但报“暂不支持”**，保证未来加入时不破坏已有代码。`try`/`catch`/`throw` 同样识别并报“E7 不支持异常”的明确诊断。

### 3.6 标准库（按负载：数学 + 字符串）
| 模块 | v1 内容 |
|---|---|
| `Math` | TS 全部 `Math.*`（`sqrt` `pow` `exp` `log` `sin`… `floor` `ceil` `round` `trunc` `hypot` `min/max`），`Math.random` 可由宿主注入种子以保证可复现；整数辅助 `idiv` `imod` `gcd` `abs` `clamp` |
| 数组 | `push pop slice splice concat indexOf includes sort(cmp) reverse fill map filter reduce some every find join`；`Float64Array/Int32Array/Uint8Array` |
| 字符串 | `length charCodeAt at slice substring indexOf lastIndexOf includes startsWith endsWith split trim toUpperCase toLowerCase padStart padEnd repeat replace/replaceAll(字符串参数)`、模板字符串 |
| 转换 | `parseInt parseFloat Number() String() toFixed toString(radix)` |
| 容器 | `Map` `Set`（插入有序，与 JS 一致） |
| 宿主 | `print`（重定向到宿主日志）、宿主注册的原生函数 |

另见 §3.8 的字节、编码与 `e7:crypto` 模块。

暂不包含：`RegExp`（v2 视需求加入）、`Date`（由宿主提供时间函数）。`bigint` 与 `JSON` 因签名场景需要已提前到 v1（见 §3.8）。

### 3.7 字符串实现
- 语义与 TS 保持一致：`length`/下标按 **UTF-16 代码单元**计算，保证与 Node 差分测试结果相同。
- 存储双表示（同 V8）：全 Latin-1 的字符串每字符 1 字节，否则 UTF-16；O(1) 随机访问。
- 不可变 + 驻留（短字符串/常量），哈希缓存在对象头中。
- 拼接优化：编译器把循环中的 `s += x` 和模板字符串降级为内部 StringBuilder，避免 O(n²)。
- 与宿主交互时按 UTF-8 收发（C API 层做转换）。

---

### 3.8 面向签名 / 验签的设计

签名类脚本通常分两层，E7 对两层分别优化：

| 层 | 典型内容 | 放在哪里执行 |
|---|---|---|
| **协议/业务层** | 参数排序与拼接、规范化 JSON、URL 编码、时间戳/随机数、Base64/Hex、选用哪个算法、组装签名头 | **E7 脚本**（可随服务器下发灵活更新） |
| **密码学原语层** | SHA-1/2/3、MD5、HMAC、HKDF/PBKDF2、AES、RSA、ECDSA、Ed25519、国密 SM2/SM3/SM4 等 | **原生 C 实现**，以内置模块暴露给脚本 |
| **自定义算法** | 私有的混淆/摘要/变换算法 | E7 脚本，走定宽整数快速路径 |

原因：标准密码算法在解释器里执行通常比原生慢一到两个数量级（RSA/ECC 这类大数运算尤其明显），而且原生实现更容易做到常量时间和经过审计；脚本只负责“怎么组合”，这正是需要动态下发的部分。

#### 3.8.1 定宽整数与位运算
- 类型：`i8 u8 i16 u16 i32 u32 i64 u64`（`int` = `i64`）。
- 运算：`+ - *` 回绕；`/ %` 整数除；`& | ^ ~ << >> >>>` 按位宽；移位量按位宽取模（与 C 的未定义行为不同，E7 明确定义）。
- 内建函数（每个都是单条 VM 指令）：`rotl32 rotr32 rotl64 rotr64 clz ctz popcnt bswap32 bswap64 mulhi64`（64×64 取高 64 位，用于大数/哈希）。
- 字面量按上下文推断：`const K: u32[] = [0x428a2f98, ...]` 中的字面量直接是 `u32`。

#### 3.8.2 字节与编码
- `Uint8Array` 是字节串的唯一标准类型；支持 `subarray`、`slice`、`set`、拼接 `concatBytes(...)`。
- `DataView`：`getUint32(off, littleEndian)` / `setUint32` 等，用于大小端读写（每个都是单条指令）。
- `TextEncoder` / `TextDecoder`（UTF-8）。
- `e7:encoding` 模块：`hex`、`base64`、`base64url`、`base32`（编码/解码）、`encodeURIComponent`。
- `JSON.stringify`（v1）：确定性输出（字段顺序 = 声明顺序；可选键排序），用于“对 JSON 体签名”；`JSON.parse` 返回 `JsonValue` 类型（带类型安全的访问方法，不引入 `any`）。

#### 3.8.3 `bigint`
- 支持 TS 的 `bigint` 类型和 `123n` 字面量，原生 C++ 大数实现。
- 额外提供 `modPow`、`modInverse`、`gcd` 等原生函数（Montgomery 乘法），便于脚本实现非标准的数论类算法。
- 标准 RSA/ECC 仍建议直接调用 `e7:crypto`，不要在脚本中用 `bigint` 手写。

#### 3.8.4 原生 `e7:crypto` 模块（同步 API）
```ts
import { sha256, hmac, ed25519, ecdsa, rsa, timingSafeEqual } from "e7:crypto";

export function sign(params: Map<string, string>, key: KeyHandle): string {
  const keys = [...params.keys()].sort();
  const canonical = keys.map(k => `${k}=${encodeURIComponent(params.get(k)!)}`).join("&");
  const mac = hmac("sha256", key, new TextEncoder().encode(canonical));
  return hex.encode(mac);
}
```
- 算法范围（按编译开关裁剪体积）：摘要 SHA-1/224/256/384/512、SHA-3/Keccak、MD5、SM3、CRC32；MAC/KDF：HMAC、HKDF、PBKDF2；对称：AES-CBC/CTR/GCM、ChaCha20-Poly1305、SM4；签名：RSA PKCS#1 v1.5 / PSS、ECDSA P-256/secp256k1、Ed25519、SM2。
- 实现来源（候选，待定）：BearSSL（MIT，常量时间、体积小，覆盖 RSA/ECDSA/AES/SHA）+ Monocypher（Ed25519/X25519/ChaCha）+ GmSSL（Apache-2.0，国密）；或统一用 mbedTLS。不建议自己从头写密码原语。
- 不使用 WebCrypto 的 `crypto.subtle`，因为它是异步（Promise）API，而 v1 没有 async。
- Node 差分测试：提供一个基于 `node:crypto` 的 `e7:crypto` 垫片，同一脚本在 Node 和 E7 上对比输出；同时用 NIST / RFC 官方测试向量验证。

#### 3.8.5 密钥句柄（密钥不进脚本）
- 宿主把密钥存放在 iOS Keychain / Android Keystore / Windows DPAPI 中，只把一个不透明的 `KeyHandle` 传给脚本。
- 脚本可以用句柄做 `hmac` / `sign`，但**拿不到密钥明文**，下发的脚本即使被截获或篡改也无法导出密钥。
- 也支持直接传入 `Uint8Array` 形式的密钥（适合验签用的公钥或测试）。
- MAC 比较必须用 `timingSafeEqual`。

#### 3.8.6 性能目标（签名场景）
- 脚本层：一次典型的“参数规范化 + HMAC-SHA256 + Hex”在中端手机上 < 1 ms。
- 纯脚本实现的 SHA-256（`u32` 快速路径）用作 VM 位运算性能的基准，目标是达到同机原生实现的 1/20 ~ 1/50。
- VM 为此提供：定宽整数全类型化指令、`rotl/rotr` 单指令、`Uint8Array`/`DataView` 读写单指令、常见组合的超级指令（如 `x = rotr32(x, n) ^ y`）。

## 4. 整体架构

```
 ┌──────────── 服务器 / 构建端（主路径） ────────────┐
 .e7 → Lexer → Parser → AST → TypeChecker → 类型化 IR → Optimizer → CodeGen
                                                  → .e7b → Ed25519 签名 → 下发
 └──────────────────────────────────────────────────┘
 ┌──────────── 设备端 ──────────────────────────────┐
 [可选] libe7c（同一份编译器源码，编译开关 E7_WITH_COMPILER）
 .e7b → 签名校验 → 格式解析 → **字节码类型验证器** → VM ⇄ GC
                                    ⇅ C API（只暴露宿主授权的原生函数）
 Swift/ObjC · Kotlin/JNI · C#/C++
 └──────────────────────────────────────────────────┘
```

- 生产环境：**服务器编译并签名**，设备只加载 `.e7b`。核心库 `libe7vm` 不含编译器，体积小、攻击面小。
- 端上编译（调试、本地热更新、离线编辑场景）：链接 `libe7c`，编译产物同样要经过验证器（验证器不信任任何来源，包括自己的编译器）。

---

## 5. VM 设计（为静态类型重新设计）

静态类型让 VM 可以做得比 Lua 这类动态 VM 更快、更简单：

### 5.1 无标签寄存器 + 全类型化指令
- 寄存器槽 8 字节，**不带类型标签**；每个函数原型携带一张“寄存器类型表”（int / number / ref …）。
- 指令按类型区分：`ADD_I` `ADD_F` `LT_I_JMP` `LOAD_F64_ELEM` `GETFIELD off` `CALL_VIRT slot` `CALL_IFACE` …
- 字段访问是**固定偏移量的一次内存读**，不需要哈希查找和内联缓存；动态 VM 需要的 quickening 也不需要了。
- 指令编码保持 32 位定长（`op:8 A:8 B:8 C:8` / `op:8 A:8 Bx:16`），超大函数用扩展前缀指令。
- 分派：GCC/Clang 用 computed goto，MSVC 回落 switch（Windows 发布推荐 clang-cl）。

### 5.2 字节码验证器（服务器下发的安全基石）
类似 JVM/Wasm：加载时对每个函数做一遍数据流类型检查，保证：
- 每条指令的操作数寄存器类型与指令要求一致（例如不能把 int 当指针解引用）；
- 常量/寄存器/字段/函数索引不越界，跳转目标合法；
- 调用参数个数和类型匹配。
验证通过后，执行期**不需要任何类型检查**，同时保证内存安全。运行时只保留必要检查：数组越界、空指针、整数除零。

### 5.3 内存与 GC
- 精确 GC：寄存器类型表 = GC 根映射，对象布局表 = 对象内指针位置。
- v1：停顿式 mark-sweep；v2：增量三色标记（降低移动端卡顿）。
- 每个 VM 实例独立堆、可设内存上限、可接自定义 allocator；实例之间无共享状态，可多线程各跑各的。
- 字符串驻留；`number[]` 等为连续存储；宿主大数组可**零拷贝**包装进脚本。

### 5.4 为协程预留（v1 不暴露语法）
1. **脚本调用脚本不使用 C 栈递归**：所有脚本帧都放在 VM 自管理的栈上，帧之间用偏移而不是裸指针（栈可扩容、可整体保存）。
2. **执行状态 = 一个 Fiber 对象**（栈 + 当前帧 + ip）。v1 只有一个主 Fiber，v2 加 `yield` / `await` 时只是多建几个。
3. **指令预算 = 天然的挂起点**：在回跳和调用处检查预算。预算耗尽时，v1 就可以返回 `E7_SUSPENDED`，宿主下一帧调用 `e7_resume` 继续执行。这样**长算法分帧执行、不卡 UI** 在 v1 就能免费获得，不需要协程语法。
4. 原生函数回调脚本的位置标记为“不可挂起边界”，规则提前定好。
5. 指令集预留 `YIELD` / `RESUME` / `AWAIT` 操作码编号。

### 5.5 `.e7b` 文件格式
```
Header : magic "E7B\0" | 格式版本 | 最低 VM 版本 | flags | 小端
Body   : 常量池 | 类型表(类/接口/记录布局) | 函数原型(寄存器类型表 + 指令) | 导入导出表
Debug  : 行号表、局部变量名（可剥离）
Trailer: SHA-256 摘要 | Ed25519 签名 | 公钥 ID
```

---

## 6. 安全与沙箱（P0）

| 层 | 措施 |
|---|---|
| 传输/来源 | `.e7b` 签名（Ed25519），宿主内置公钥，验签失败拒绝加载；支持密钥轮换（公钥 ID） |
| 结构 | 格式解析全部带边界检查；对解析器和验证器做 **fuzz 测试**（libFuzzer） |
| 类型 | 字节码验证器（§5.2） |
| 能力 | 默认无文件/网络/系统调用；脚本只能调用宿主显式注册的原生函数（白名单） |
| 资源 | 指令预算、内存上限、调用深度上限、墙钟超时；超限时安全终止并返回错误 |
| 版本 | VM 拒绝加载高于自身支持版本的字节码；服务器按客户端 VM 版本下发对应产物 |
| 合规 | iOS 不使用 JIT；纯解释执行，下发脚本不改变 App 主要用途 |
| 加固（可选，v1.x） | 若签名算法本身需要防逆向：每个发布版本随机打乱操作码编号、常量池/字符串加密、剥离调试信息、控制流平坦化；VM 自身做完整性校验。注意：客户端算法无法做到绝对保密，加固只能提高逆向成本 |

---

## 7. 嵌入 API（C 接口草案）

```c
e7_vm*  e7_new(const e7_config* cfg);   // 内存上限、指令预算、allocator、受信公钥
void    e7_free(e7_vm*);
int     e7_load(e7_vm*, const uint8_t* e7b, size_t len, e7_module** out);  // 验签 + 验证
int     e7_compile(const char* src, size_t len, e7_buffer* out, e7_diag* diag); // 需 libe7c
int     e7_call(e7_vm*, e7_module*, const char* fn, const e7_value* args, int argc, e7_value* ret);
int     e7_resume(e7_vm*, e7_value* ret);          // 预算耗尽后继续执行
void    e7_register_native(e7_vm*, const char* name, const char* signature, e7_native_fn, void* ud);
e7_value e7_wrap_f64_array(e7_vm*, double* data, size_t n, e7_release_fn release);
const char* e7_last_error(e7_vm*);                 // 含源码行号的调用栈
```
原生函数注册时带类型签名（如 `"(number[],int)->number"`），编译期和加载期都会检查脚本调用是否匹配。
上层封装：iOS → Swift Package + xcframework；Android → AAR（JNI + Kotlin）；Windows → DLL / C# P/Invoke。

---

## 8. 工具链
- `e7.d.ts`：标准库与 `int`/`i32` 等类型声明，让 VS Code 原生支持 E7。
- `e7c`：编译器命令行（`--check` 只做类型检查、`-O2`、`--strip-debug`、`--sign key`）。
- `e7`：运行器 / 反汇编器（`e7 dis a.e7b`）/ 基准工具。
- 测试：单元测试 + `.e7` 用例（期望输出）+ **Node.js 差分测试** + fuzz。
- 基准：数值（fib、nbody、spectral-norm、mandelbrot、matmul、sieve、quicksort）+ 字符串（拼接、split/join、查找替换、词频统计 Map<string,int>）；对照 Lua 5.4、QuickJS、Node（解释模式 `--jitless`）。

## 9. 目录结构（规划）
```
include/e7/e7.h       对外 C API
src/common/           内存、字符串、容器
src/compiler/         lexer parser ast checker ir optimizer codegen   → libe7c
src/vm/               loader verifier interp gc object fiber natives  → libe7vm
src/stdlib/           Math String Array Map 等
tools/e7c tools/e7    命令行
lib/e7.d.ts           IDE 类型声明
tests/ bench/ fuzz/
platform/ios android windows
```

## 10. 里程碑
| 阶段 | 内容 | 验收 |
|---|---|---|
| M0 规范 | 冻结 v1 语法子集、类型规则、数值语义、指令集、`.e7b` 格式 | 本文档定稿 + `e7.d.ts` |
| M1 前端 | Lexer、Parser（TS 子集）、AST、诊断信息 | 示例脚本解析通过，非法语法报错准确 |
| M2 类型检查 | 推断、类/接口/泛型/可空检查 | 用例与 tsc 判定一致（E7 更严格处有专门用例） |
| M3 后端+VM | IR、寄存器分配、CodeGen、解释器、基本类型与函数 | fib / 循环 / 快排跑通，Node 差分测试通过 |
| M4 对象与 GC | class/interface/闭包/数组/Map/字符串、mark-sweep | 综合用例通过，无泄漏（ASan/LSan） |
| M5 字节码与安全 | `.e7b` 序列化、验证器、签名、沙箱、预算挂起/恢复 | fuzz 24h 无崩溃；篡改字节码被拒 |
| M5.5 签名能力 | 定宽整数指令、`Uint8Array`/`DataView`、`e7:encoding`、`e7:crypto`（接入原生库）、`bigint`、密钥句柄 | 全部算法通过 NIST/RFC 测试向量；与 Node 垫片差分一致 |
| M6 性能 | 常量折叠、融合比较跳转、内联小函数、超级指令 | 基准达到或超过 Lua 5.4；脚本 SHA-256 达到 §3.8.6 目标 |
| M7 三端集成 | C API、iOS/Android/Windows 构建与 Demo App、端上编译 | 三端真机运行同一 `.e7b` 结果一致 |
| E8（v2） | 协程（`yield`/`async`）、增量 GC、getter/setter、RegExp | — |

## 11. 决策记录
| 问题 | 决定 |
|---|---|
| 命名 | 语言 E7，引擎 e7vm |
| 语法 | 只用严格 TS 子集 |
| 整数默认 | **与 JS 一致**：不标注时为 `number`；定宽整数 `u32`/`u64` 等显式标注（v0.4 确认，见 §3.2） |
| 异常 | 不支持；运行时错误终止调用并返回宿主 |
| 负载 | 签名/验签为主，兼顾数学与字符串 |
| 密码原语 | 原生实现、以 `e7:crypto` 暴露；脚本负责组合逻辑 |

## 12. 下一步（M0 规范冻结需要产出）
1. `docs/SPEC.md`：E7 语法子集（EBNF）、类型规则、数值与字符串语义、错误模型。
2. `docs/BYTECODE.md`：指令集表、编码、`.e7b` 格式、验证规则。
3. `lib/e7.d.ts`：标准库与 `int` 等类型声明，可直接在 VS Code 中试写 E7 脚本。
4. `examples/`：10 个左右典型脚本，同时作为第一批测试与基准：API 参数签名（HMAC）、JSON 体签名、RSA/ECDSA/Ed25519 验签、纯脚本 SHA-256、自定义位运算摘要、Base64/Hex、大数模幂，以及若干数值与字符串算法。

### 待确认
1. 需要哪些具体算法？尤其是否需要**国密 SM2/SM3/SM4**、secp256k1。
2. 签名算法本身是否需要**防逆向加固**（§6 加固一行）？
3. 密钥是否由宿主通过 Keychain/Keystore 管理、以句柄形式传给脚本？

## 参考资料
- Static TypeScript 论文（MPLR 2019）：https://mmoskal.github.io/pdf/static-typescript.pdf
- Rocket-fast embedded TypeScript for MakeCode Arcade：https://www.microsoft.com/en-us/research/blog/rocket-fast-embedded-typescript-for-makecode-arcade/
- MakeCode 语言说明：https://makecode.com/language
- E 语言（命名冲突参考）：https://en.wikipedia.org/wiki/E_(programming_language)
- Eve 语言（witheve，命名冲突参考）：https://github.com/witheve/Eve
- e8vm（命名冲突参考）：https://github.com/ryanphuang/e8
- BearSSL：https://bearssl.org/
- Monocypher：https://monocypher.org/
- GmSSL（国密）：https://github.com/guanzhi/GmSSL
- Mbed TLS：https://github.com/Mbed-TLS/mbedtls
