# E7 语言与 e7fvm 虚拟机设计方案（草案 v0.2）

> v0.2 变更：根据反馈改为 **严格静态类型**、语法以 **TypeScript 严格子集** 为主；明确 **脚本由服务器下发**、**端上带编译器（可选）**、**为协程预留设计**；新增命名调研。
> 现阶段只做设计，不开发。

已确认的需求：

| 项 | 结论 |
|---|---|
| 类型 | 严格静态类型，无 `any` |
| 语法 | TypeScript 子集（或同等优雅的 Ruby 风格），要求“写得少、表达得多” |
| 脚本来源 | **服务器下发** → 签名校验 + 字节码验证 + 沙箱是 P0 |
| 编译器 | 核心是 VM 跑 `.e7b`；编译器也要能在手机上跑（可选模块） |
| 协程 | v1 不提供语法，但 VM 架构从第一天按“可挂起”设计 |
| 平台 | Android / iOS / Windows，C++17 |

---

## 1. 命名调研

### 1.1 一个发现：`e7f` 就是 `everettjf` 的缩写
`everettjf` = **e** + `verettj`（7 个字母）+ **f** → **e7f**，与 `i18n`（internationalization）、`k8s`（kubernetes）是同一种数字缩写法（numeronym）。
仓库名 `e7fvm` 正好可以解读为 **e7f VM = everettjf 的虚拟机**。这个名字和你的 ID 有天然关系，而且有“梗”可讲。

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

### 1.3 推荐命名体系
- 语言：**E7**（读作 “E-seven”）
- 引擎/仓库：**e7fvm**（e7f = everettjf）
- 源文件 `.e7`，字节码 `.e7b`，编译器命令 `e7c`，运行器 `e7`
- 版本演进可以自然地留给 **E8**（例如引入协程/泛型大版本时）

---

## 2. 语法方案调研

### 2.1 可参考的“静态类型脚本”先例

| 项目 | 做法 | 对我们的启示 |
|---|---|---|
| **Static TypeScript（微软 MakeCode，MPLR 2019 论文）** | TS 的语法子集，类改为**名义类型**，编译到 ARM 机器码，mark-sweep GC；对象访问密集的基准比 MicroPython/Duktape 快 ≥20 倍，比 V8 慢不到 2 倍 | **最直接的先例**：证明“严格 TS 子集 + 静态编译”可行且很快 |
| AssemblyScript | TS 语法 + `i32/i64/f64` 等数值类型（通过声明文件让 tsc 认识），编译到 Wasm | 用 `.d.ts` 声明额外数值类型，**IDE 白拿** |
| Crystal | Ruby 语法 + 静态类型 + 全局类型推断 | Ruby 风格也能静态化，但需要自研全部工具链 |
| Swift / Kotlin | 现代静态类型语言的表达力标杆 | 借鉴其可空类型 `T?`、表达式风格 |
| Wasm / JVM | 加载时做字节码类型验证 | 服务器下发字节码的安全模型 |

### 2.2 方案 A：E7 = 严格 TypeScript 子集（推荐）

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

代价：花括号和分号让代码比 Ruby 略“重”；TS 的结构化类型、`any`、原型链等必须裁剪掉（见 §3）。

### 2.3 方案 B：Ruby 风格静态语言（Crystal 式）

```ruby
def quicksort(a : Array(Float), lo = 0, hi = a.size - 1)
  return if lo >= hi
  p = a[(lo + hi) >> 1]
  i, j = lo, hi
  while i <= j
    i += 1 while a[i] < p
    j -= 1 while a[j] > p
    if i <= j
      a[i], a[j] = a[j], a[i]
      i += 1; j -= 1
    end
  end
  quicksort(a, lo, j)
  quicksort(a, i, hi)
end

class Point(x : Float, y : Float)        # 一行定义类
  def dist(o : Point) = Math.hypot(x - o.x, y - o.y)
end

total = pts.map { |p| p.x + p.y }.sum
```

优点：确实更短更优雅。缺点：没有现成 IDE 支持（要自研 LSP、高亮）、没有现成的“参照实现”做差分测试、学习人群更小、语法设计和类型推断规则全部要自己定，工作量和风险明显更大。

### 2.4 结论
- **v1 采用方案 A（严格 TS 子集）**，用类型推断和精选语法特性保证简洁。
- 编译器架构上把“前端”（词法/语法 → 类型化 AST）和“后端”（类型化 IR → 字节码）解耦。如果以后想要 Ruby 风格，可以**再加一个前端**，复用全部后端和 VM，两种语法编译出同一种 `.e7b`。

---

## 3. E7 语言规范要点（v1）

### 3.1 类型系统
| 类别 | 内容 | 说明 |
|---|---|---|
| 基本类型 | `boolean` `int` `number` `string` | `number` = f64（与 TS 一致）；`int` = i64，在 `e7.d.ts` 中声明为 `type int = number`，所以 tsc 也认识 |
| 存储类型 | `i32` `u8` `f32` 等 | 仅用于数组元素/字段以节省内存 |
| 数组 | `T[]`、`Float64Array`、`Int32Array` … | 静态类型下 `number[]` **本身就是连续 f64 存储**，无装箱 |
| 容器 | `Map<K,V>` `Set<T>` | 内置泛型 |
| 类 | `class`（单继承、`implements`） | **名义类型**，字段布局编译期固定，方法走 vtable |
| 接口 | `interface` | 编译期结构检查，运行时用 itable 分派 |
| 记录 | `type P = { x: number, y: number }` / 对象字面量 | 编译为匿名定长结构体 |
| 可空 | `T \| null`、`?.`、`??`、`!` | 只有 `null`，**没有 `undefined`** |
| 枚举 | `enum`、字符串字面量联合 `"a" \| "b"` | 编译为整数 |
| 函数 | 函数类型、箭头函数、闭包、默认参数、元组返回 `[int, string]` | |
| 泛型 | 泛型函数和类 | 按“值类别”单态化（int/number/引用各生成一份） |
| 模块 | `import` / `export` | 编译期链接 |

### 3.2 数值语义（算法执行器的关键）
- 整数字面量推断为 `int`，带小数点的推断为 `number`。
- `int` 可隐式提升为 `number`；`number → int` 必须显式（`Math.floor` / `Math.trunc` / `toInt`）。
- `int + - * % 位运算` 结果仍是 `int`；**`int / int` 结果是 `number`**，与 JS 一致；整除用 `Math.floor(a / b)` 或 `idiv(a, b)`（编译器识别为单条整除指令）。
- `int` 是 64 位、溢出回绕。在 ±2^53 以内与 JS 结果完全一致（差分测试覆盖此范围）。
- 不开 fast-math，保证三端浮点结果一致（确定性）。

### 3.3 刻意不支持（相对 TS）
`any` / `unknown` 的随意转换、`undefined`、`==`（只允许 `===`）、`var`、原型链/`prototype`、`eval`/`Function()`、运行时增删属性、`delete`、getter/setter（v1）、装饰器、`with`、宽松的结构化赋值转换、声明合并。

### 3.4 预留关键字（为协程准备）
`function*` / `yield`、`async` / `await` 在 v1 解析器中**识别但报“暂不支持”**，保证未来加入时不破坏已有代码。

---

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
- 基准：fib、nbody、spectral-norm、quicksort、matmul、字符串处理；对照 Lua 5.4、QuickJS。

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
| M6 性能 | 常量折叠、融合比较跳转、内联小函数、超级指令 | 基准达到或超过 Lua 5.4 |
| M7 三端集成 | C API、iOS/Android/Windows 构建与 Demo App、端上编译 | 三端真机运行同一 `.e7b` 结果一致 |
| E8（v2） | 协程（`yield`/`async`）、增量 GC、getter/setter、可能的 Ruby 风格前端 | — |

## 11. 还需要确认
1. 命名：语言叫 **E7**、引擎叫 **e7fvm** 是否 OK？还是更喜欢 E7F / Evr？
2. 语法：同意 v1 走**严格 TS 子集**（方案 A），把 Ruby 风格留作将来可选的第二前端吗？
3. 整数默认：整数字面量推断为 `int`（i64）是否 OK？（另一种是全部默认 `number`，更像 JS 但循环更慢）
4. 异常：v1 是否需要 `try/catch/throw`？（不需要的话，错误直接终止脚本并返回宿主，更简单）
5. 主要算法负载类型（数值计算 / 字符串 / 图与规则），用来决定标准库和基准的优先级。

## 参考资料
- Static TypeScript 论文（MPLR 2019）：https://mmoskal.github.io/pdf/static-typescript.pdf
- Rocket-fast embedded TypeScript for MakeCode Arcade：https://www.microsoft.com/en-us/research/blog/rocket-fast-embedded-typescript-for-makecode-arcade/
- MakeCode 语言说明：https://makecode.com/language
- E 语言（命名冲突参考）：https://en.wikipedia.org/wiki/E_(programming_language)
- Eve 语言（witheve，命名冲突参考）：https://github.com/witheve/Eve
- e8vm（命名冲突参考）：https://github.com/ryanphuang/e8
