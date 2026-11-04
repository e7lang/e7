# e7fvm 设计调研与规划（草案 v0.1）

> 目标：用 **C++17** 实现一个可嵌入的轻量脚本语言 + 字节码虚拟机，作为**算法执行器**运行在 **Android / iOS / Windows** 上。
> 脚本先**离线编译成字节码（中间语言）**，再分发给各端 VM 执行。

---

## 0. 结论速览（TL;DR）

| 决策点 | 建议 | 理由 |
|---|---|---|
| 实现语言 | C++17，关闭 RTTI，核心路径不用异常；对外暴露 **C API** | 方便 Swift/ObjC、JNI/Kotlin、C# 绑定；编译器全平台可用 |
| 语法风格 | **Ruby/Lua 式 `end` 块** + `let` 显式声明 + 可选类型标注 | 比 Python 缩进更适合嵌入（脚本常被放进字符串/JSON/网络下发，缩进易损坏）；比 Ruby 更简单、可静态解析 |
| 执行方式 | **源码 → AST → 寄存器式字节码 (.e7b)**，VM 解释执行 | 寄存器式 VM 比栈式少 ~30–45% 指令分派，Lua 5 / Dalvik 已验证 |
| JIT | **不做** | iOS 禁止普通 App 申请可执行内存（W^X），JIT 无法跨平台；把精力放在解释器优化 |
| 分派方式 | GCC/Clang 用 **computed goto**，MSVC 回落 `switch` | Windows 建议用 clang-cl 构建以获得同等性能 |
| 值表示 | 16 字节 tagged union（完整 int64 + f64 + 指针） | 算法场景需要精确 int64；NaN-boxing 无法无损放 int64 |
| 数值类型 | `int`(i64) 与 `float`(f64) 区分 | 算法正确性（整除、位运算、溢出语义可控） |
| 内存管理 | 精确 **标记-清除 GC**（后续可加增量/分代），配合类型化数组减少分配 | 实现简单、无循环引用问题 |
| 性能手段 | 编译期常量折叠/寄存器分配 + 运行时 **quickening**（类型特化指令）+ 内联缓存 + `f64[]/i32[]` 等类型化数组 | 无 JIT 前提下最有效的几招 |
| 安全 | 默认沙箱：无文件/网络 IO，指令预算、内存上限、字节码加载校验 | 脚本可能来自网络下发 |
| 构建 | CMake；产物：iOS xcframework、Android `.so`（arm64-v8a/armeabi-v7a/x86_64）、Windows `.dll/.lib` | 一套源码 |

性能预期：做到 **Lua 5.4 解释器同量级**是现实目标；紧密数值循环一般比原生 C 慢 10–50 倍。真正的热点可通过“原生扩展函数”注册进 VM（C++ 实现，脚本调用）。

---

## 1. 调研：可参考的现有引擎

| 引擎 | 语言 | VM 类型 | 可借鉴点 | 不足（对本项目） |
|---|---|---|---|---|
| **Lua 5.4** | C | 寄存器式 | 32 位定长指令编码、upvalue 闭包、table、极小体积，嵌入 API 标杆 | 1 起始下标、全局变量默认、语法较老 |
| LuaJIT（解释器部分） | C+asm | 寄存器式 | 手写汇编解释器、NaN-boxing、类型特化 | 汇编不可移植，JIT 在 iOS 不可用 |
| **Wren** | C | 栈式 | 类为中心、优雅语法、computed goto、单遍编译器、fiber | 栈式，偏 OOP 而非数值 |
| **Crafting Interpreters (clox)** | C | 栈式 | 最清晰的入门实现：扫描器→Pratt 解析→字节码→GC | 教学性质，性能一般 |
| **pocketpy** | C++17→C11 | 栈式 | 类 Python 语法、面向游戏/移动端嵌入 | 缩进语法、动态性高导致优化难 |
| mruby | C | 寄存器式 | Ruby 语法、可预编译 `.mrb` 字节码（与本项目思路一致） | 语义庞大、体积偏大 |
| Squirrel / Gravity / Berry | C/C++ | 栈/寄存器 | 游戏/IoT 嵌入经验、引用计数方案 | 生态小 |
| QuickJS | C | 栈式 | 字节码序列化、完整 GC、可移植性 | JS 语义过重 |
| Umka | C | 栈式 | **静态类型**脚本，类型化后指令更快 | 语法 Go 风格 |

**取舍**：架构参考 Lua 5.4（寄存器 VM、指令编码、闭包/upvalue）；编译器结构参考 clox/Wren（Pratt 解析）；字节码离线序列化参考 mruby/QuickJS；语法取 Ruby 的简洁 + Lua 的可预测。

---

## 2. 语法设计（草案）

设计原则：
1. **块用 `end` 结束，不依赖缩进** —— 脚本可安全地压缩、拼接、通过网络/JSON 传输。
2. **`let` 显式声明局部变量**，无隐式全局 —— 所有变量在编译期解析为寄存器槽位，零运行时查找；拼写错误在编译期报错。
3. **表达式导向**：`if`/`match` 可作为表达式；函数最后一个表达式是返回值（可选，同时支持 `return`）。
4. **可选类型标注** `: int / float / f64[]` —— 不写也能跑；写了编译器能直接生成特化指令。
5. 0 起始下标，区间 `0..n`（不含 n）、`0..=n`（含 n）。

### 2.1 示例

```ruby
# 注释用 #

let PI = 3.14159265358979          # 模块级常量

fn gcd(a: int, b: int) -> int
  while b != 0
    a, b = b, a % b
  end
  a                                # 最后一个表达式即返回值
end

fn quicksort(a, lo = 0, hi = len(a) - 1)
  return if lo >= hi               # 后置 if（Ruby 风格）
  let p = a[(lo + hi) / 2]
  let i, j = lo, hi
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

# 类型化数组：连续内存、无装箱，算法热点用它
fn dot(x: f64[], y: f64[]) -> float
  let s = 0.0
  for i in 0..len(x)
    s += x[i] * y[i]
  end
  s
end

# 闭包 / lambda
let squares = map(1..=10, |x| x * x)
let evens   = [1, 2, 3, 4].filter(|x| x % 2 == 0)

# 结构体（轻量记录类型，字段编译期定偏移）
struct Point
  x: float
  y: float
end

fn dist(a: Point, b: Point)
  let dx = a.x - b.x
  let dy = a.y - b.y
  sqrt(dx * dx + dy * dy)
end

# match 表达式
let kind = match n
  0        => "zero"
  1..10    => "small"
  _        => "big"
end
```

### 2.2 v1 语言范围（刻意保持小）
- 类型：`nil` `bool` `int` `float` `string` `array` `map` 类型化数组(`i32[] i64[] f32[] f64[] u8[]`) `struct` `function/closure`
- 控制流：`if/elif/else` `while` `for in` `break/continue` `return` `match`
- 函数：默认参数、多返回值（元组解构）、闭包
- 模块：`import "math"`（编译期链接进同一字节码包或由宿主提供）
- **暂不做**：类继承、异常（用 `(value, err)` 返回或 `error()` 终止执行）、协程（v2 可加，用于分步执行长算法）

---

## 3. 整体架构

```
          ┌──────────── 编译端（PC/构建服务器，或端上按需） ───────────┐
 .e7 源码 → Lexer → Parser(Pratt) → AST → Resolver(作用域/类型推断)
                                         → CodeGen(寄存器分配) → Optimizer(常量折叠/窥孔)
                                         → Serializer → .e7b 字节码包
          └───────────────────────────────────────────────────────────┘
                                     │ 分发（随包 / 网络下发）
          ┌──────────── 运行端（Android / iOS / Windows） ─────────────┐
 .e7b → Loader + Verifier(校验) → VM(解释器循环) ⇄ GC/Heap
                                        ⇅
                         C API ← 宿主（Swift/ObjC、JNI/Kotlin、C#、C++）
          └───────────────────────────────────────────────────────────┘
```

- **编译器与 VM 解耦**：运行端可以只链接 VM（更小、无解析器攻击面），也可以带编译器支持端上直接跑源码（调试方便）。
- 编译选项：`e7c script.e7 -o script.e7b [-O2] [--strip-debug]`。

---

## 4. 字节码与 VM

### 4.1 指令编码（32 位定长，参考 Lua 5.4）
```
 iABC : | op:8 | A:8 | B:8 | C:8 |      R[A] = R[B] op R[C]
 iABx : | op:8 | A:8 |   Bx:16   |      常量索引、全局槽
 iAsBx: | op:8 | A:8 |  sBx:16   |      跳转偏移
```
- 每个函数最多 256 个寄存器（局部变量 + 临时值）。
- 带 K 版本的指令（`ADDK R[A] = R[B] + K[C]`）减少常量加载。

### 4.2 指令集草案（约 60–80 条）
- 数据移动：`MOVE LOADK LOADI LOADF LOADNIL LOADBOOL GETUPVAL SETUPVAL GETGLOBAL SETGLOBAL`
- 算术（通用 + 特化）：`ADD SUB MUL DIV IDIV MOD POW NEG` 及 `ADD_II ADD_FF ADDK_I ...`
- 位运算：`BAND BOR BXOR SHL SHR BNOT`
- 比较跳转（融合）：`LT_JMP LE_JMP EQ_JMP`、`LTI_JMP`（与立即数比较）
- 循环：`FORPREP FORLOOP`（数值 for 专用，计数器放寄存器，无分配）
- 容器：`NEWARR NEWMAP GETIDX SETIDX GETIDX_F64 SETIDX_F64 GETFIELD SETFIELD LEN`
- 调用：`CALL TAILCALL RETURN CLOSURE CALLNATIVE`
- 其他：`CONCAT MATCH_RANGE NOP HALT`

### 4.3 解释器循环
- `Value* R`（当前帧寄存器基址）、`const Instr* ip`、`Value* K` 常驻局部变量 → 编译器放进 CPU 寄存器。
- GCC/Clang：`static void* dispatch[] = {&&op_MOVE, ...}; goto *dispatch[op];`
- MSVC：`switch` 回落（宏统一两种写法）。
- 调用不走 C 递归（脚本函数调用在同一个循环内压帧），避免栈溢出，也便于以后加协程。

### 4.4 Quickening（运行时指令特化）
通用 `ADD` 首次执行时看到两个 int → 原地改写为 `ADD_II`；类型不符时退回通用版本。
字段访问 `GETFIELD` 用**内联缓存**（缓存 shape → 偏移）。
这是无 JIT 前提下性价比最高的优化。

### 4.5 .e7b 文件格式
```
magic "E7B\0" | version u16 | flags u16 | 小端固定
常量池（int/float/string） | 函数原型表（参数数、寄存器数、upvalue 描述、指令数组）
调试信息段（行号表，可剥离） | 校验和 CRC32
```
加载时 **Verifier** 检查：寄存器/常量索引越界、跳转目标合法、版本兼容 —— 保证恶意或损坏字节码不会让 VM 越界访问内存。

---

## 5. 值表示与内存

```cpp
struct Value {            // 16 字节
  Type tag;               // NIL BOOL INT FLOAT OBJ ...
  union { int64_t i; double f; bool b; Obj* o; };
};
```
- 堆对象（`String Array Map TypedArray Struct Closure Proto Upvalue`）统一 `Obj` 头：类型 + GC 标记 + next 链。
- 字符串驻留（intern）→ 字符串比较、map 键查找 O(1)。
- **GC**：v1 精确停顿式 mark-sweep，按分配量触发；v2 改增量（三色标记）以降低移动端卡顿。
- 支持**每个 VM 实例独立堆 + 自定义 allocator**（宿主可限制内存上限）。
- 多线程模型：一个 `e7_vm` 实例单线程使用；多个实例可在不同线程并行（无全局状态）。

---

## 6. 嵌入 API（C 接口，草案）

```c
e7_vm*  e7_new(const e7_config* cfg);           // 内存上限、指令预算、allocator
void    e7_free(e7_vm*);
int     e7_load_bytecode(e7_vm*, const uint8_t* data, size_t len, e7_module** out);
int     e7_compile(e7_vm*, const char* src, size_t len, e7_buffer* out_bc, e7_error* err); // 可选
int     e7_call(e7_vm*, e7_module*, const char* fn, const e7_value* args, int argc, e7_value* ret);
void    e7_register_native(e7_vm*, const char* name, e7_native_fn fn, void* userdata);
// 零拷贝传大数组：宿主内存直接包装成 f64[]
e7_value e7_wrap_f64_array(e7_vm*, double* data, size_t n, e7_release_fn release);
const char* e7_last_error(e7_vm*);             // 含脚本行号的错误栈
```
上层绑定：iOS 用 Swift 包装（xcframework + module map），Android 用 JNI + Kotlin 封装（AAR），Windows 直接 C/C++ 或 C# P/Invoke。

---

## 7. 跨平台与合规注意
- **iOS**：无 JIT，纯解释执行合规；App Store 条款允许解释执行脚本，但下发脚本不得改变 App 主要用途。
- **Android**：NDK + CMake；32 位 armeabi-v7a 也需支持（指针 4 字节，Value 仍 16 字节，无影响）。
- **Windows**：MSVC 可编但无 computed goto，推荐 clang-cl 发布构建。
- 统一：只依赖 C++17 标准库子集；禁止平台相关头文件进入核心；浮点行为统一（不开 `-ffast-math`，保证各端算法结果一致）。

---

## 8. 目录结构（规划）

```
e7fvm/
  include/e7/e7.h          # 对外 C API
  src/
    common/                # 内存、字符串、哈希表、工具
    compiler/              # lexer parser ast resolver codegen optimizer
    vm/                    # value object gc vm(dispatch) loader verifier
    stdlib/                # math array string 内置库
  tools/e7c/               # 命令行编译器
  tools/e7/                # 命令行运行器 / REPL
  tests/                   # 单元测试 + 脚本用例(.e7 + 期望输出)
  bench/                   # 基准：fib、nbody、spectral-norm、quicksort、matmul（对比 Lua 5.4）
  platform/ios android windows
  CMakeLists.txt
```

---

## 9. 里程碑

| 阶段 | 内容 | 验收 |
|---|---|---|
| M1 骨架 | CMake、Lexer、Pratt Parser、AST、错误报告（带行列号） | 能解析示例脚本并打印 AST |
| M2 最小 VM | 寄存器分配、CodeGen、int/float/bool/nil、算术、if/while/for、函数调用、computed goto 循环 | `fib(30)`、循环求和跑通 |
| M3 数据结构 | string/array/map/struct、闭包与 upvalue、GC | 快排、哈希计数等用例通过 |
| M4 字节码文件 | 序列化/反序列化、Verifier、`e7c` 工具 | 编译后 .e7b 在另一进程加载执行结果一致 |
| M5 性能 | 常量折叠、融合比较跳转、quickening、类型化数组、内联缓存 | 基准接近 Lua 5.4 |
| M6 嵌入与移植 | C API、沙箱（指令预算/内存上限）、iOS/Android/Windows 构建与示例 App | 三端真机跑通同一 .e7b |
| M7 完善 | 标准库、调试信息/栈回溯、fuzz 测试（解析器与 Verifier）、文档 | — |

---

## 10. 待确认问题
1. 语法风格：是否接受本文的 “`end` 块 + `let` 声明 + 可选类型” 方案？还是更偏 Python 缩进？
2. 类型系统：纯动态（+可选标注）即可，还是希望**强制静态类型**（性能更高、实现更复杂）？
3. 脚本来源：是否会从服务器下发？（决定 Verifier/沙箱的优先级）
4. 端上是否需要编译器（直接跑源码），还是只带 VM 跑 .e7b？
5. 算法场景的典型负载：数值计算（矩阵/信号）、还是字符串/图/规则匹配为主？（决定优先优化哪类指令和内置库）
6. 是否需要协程/分步执行（例如长算法分帧执行不卡 UI）？
