# E7 语言规范（v1.0）

E7 是 TypeScript 的**严格子集**：每个能通过 E7 编译器的程序，都能被 `tsc --strict`（配合 `lib/e7.d.ts`）接受，
并且在 JavaScript 引擎上运行得到相同结果（定宽整数除外，见 §4.2）。反过来不成立：E7 拒绝无法静态确定类型、
或无法高效/安全编译的 TS 写法。

源文件扩展名 `.ts`（推荐，IDE 直接支持）或 `.e7`；编译产物 `.e7b`。

---

## 1. 词法

- 注释 `//`、`/* */`；自动分号插入（ASI）与 JS 相同（含 `return` 后换行的限制）。
- 标识符仅限 ASCII（`[A-Za-z_$][A-Za-z0-9_$]*`）；字符串内容可以是任意 Unicode。
- 数字：十进制、`0x`/`0o`/`0b`、指数、数字分隔符 `1_000`；**bigint** 字面量 `123n`。不允许旧式八进制 `017`。
- 字符串：`'...'`、`"..."`，转义 `\n \t \r \b \f \v \0 \xHH \uHHHH \u{H...}`；模板字符串 `` `a ${expr} b` ``（可嵌套）。
- **不支持**：正则字面量、私有名 `#x`、装饰器 `@`。

## 2. 类型

| 类型 | 说明 | 运行时表示 |
|---|---|---|
| `number` | IEEE-754 双精度，语义同 JS | 寄存器 f64 |
| `boolean` | | 寄存器 int |
| `string` | 不可变，UTF-16 语义（`length`/下标按 UTF-16 代码单元） | 堆对象 |
| `bigint` | 任意精度整数 | 堆对象 |
| `i8 u8 i16 u16 i32 u32 i64 u64`，`int`=`i64` | **E7 扩展**：机器整数（§4.2） | 寄存器 int64（按位宽规范化） |
| `T[]` / `Array<T>` | 动态数组，元素类型化存储 | 堆对象 |
| `[A, B, ...]` | 元组（定长，各元素类型独立） | 定长记录 |
| `{ a: T; b?: U }`、`interface`、`type` 别名 | 结构化对象类型（字段按名字访问，带内联缓存） | 记录/类实例 |
| `class C` | 名义类型，字段布局编译期固定，方法虚分派 | 实例 |
| `enum` | 数字或字符串枚举（编译期常量） | number / string |
| `(a: A) => R` | 函数类型（闭包） | 闭包对象 |
| `T \| null`、`T \| undefined`、`T \| null \| undefined`，可选 `x?: T` | 可空类型 | 引用；基本类型自动装箱 |
| `Map<K, V>`、`Set<T>` | 插入有序，SameValueZero 比较键 | 堆对象 |
| `Uint8Array`、`ArrayBuffer`、`DataView` | 字节缓冲与视图（`subarray` 共享内存） | 堆对象 |
| `TextEncoder`、`TextDecoder` | UTF-8 编解码 | — |
| `KeyHandle`（来自 `e7:crypto`） | 宿主持有的密钥引用 | 句柄 |
| `void`、`never`、`null`、`undefined` | | |

**不支持**（编译错误并给出提示）：`any`、`unknown`、`object`、除 `null/undefined` 之外的联合类型、交叉类型、
用户自定义泛型（函数/类/接口/别名）、索引签名（请用 `Map<K, V>`）、`keyof`/`typeof` 类型运算、条件/映射类型、
`Record/Partial/Pick` 等工具类型、`Promise`、`Symbol`、`RegExp`、`Date`（仅 `Date.now()`）、`Int32Array` 等其他
类型化数组（请用 `u32[]` 或 `DataView`）。字符串字面量类型（`"a" | "b"`）按 `string` 处理。

### 2.1 赋值兼容
- 相同类型；`null`/`undefined` → 对应的可空类型；`T` → `T | null | undefined`。
- 子类 → 父类；类/对象类型 → 接口（**结构化**：目标的每个属性在源中存在且类型相同，每个方法签名相同）。
- 引用元素的数组协变（`Circle[]` → `Shape[]`，与 TS 一致）。
- 数字枚举 → `number`，字符串枚举 → `string`。
- 函数类型：参数与返回类型需一致；箭头函数作为回调时可以**少写参数**（多余实参被忽略），返回 `void` 的上下文接受任何返回值。
- 定宽整数与 `number`、不同宽度整数之间**不会隐式转换**，需要 `u32(x)`、`Number(x)` 等显式转换。

### 2.2 类型推断与收窄
- 变量、返回值、箭头函数参数（上下文类型）、对象/数组字面量均可推断；空数组 `[]` 需要标注或上下文类型。
- 递归函数若省略返回类型会报错，要求显式标注。
- 收窄：`x === null`、`x !== undefined`、`x == null`、真值判断 `if (x)`、`instanceof`、`&&`/`||`/`!` 组合、
  提前 `return/throw/break/continue`、`x ??= v`、赋值。支持属性路径（`this.head !== null` 之后可直接用 `this.head.next`）。
  循环中被重新赋值的变量在循环条件处重新判断；闭包内只保留 `const` 变量的收窄（同 TS）。

## 3. 声明与语句

- `let` / `const`（**不支持 `var`**）；块级作用域；循环中 `let` 每次迭代新绑定（闭包捕获行为同 JS）。
- 数组/对象/元组解构（声明、赋值 `[a, b] = [b, a]`、函数参数、`for...of`）；不支持默认值与 `...rest` 解构。
- `if / else`、`while`、`do...while`、`for(;;)`、`for...of`（数组、字符串按码点、`Uint8Array`、`Map`、`Set`、
  `arr.entries()/keys()/values()`、`map.keys()/values()/entries()`），`switch`（支持贯穿），`break/continue`（含标签），
  标签块。**不支持** `for...in`、`with`、`try/catch/finally`。
- 函数声明会提升；嵌套函数与箭头函数是闭包。
- `throw new Error(msg)` / `throw "msg"`：**终止脚本**（E7 没有异常捕获），宿主得到 `E7_ERR_RUNTIME` 和带行号的调用栈。

## 4. 表达式语义

### 4.1 `number`（与 JS 完全一致）
- 整数字面量默认是 `number`。`/` 为浮点除，`%` 同 JS（符号随被除数，含 `-0`）。
- `& | ^ ~ << >> >>>` 先 ToInt32/ToUint32（同 JS），结果为 `number`。
- `NaN`、`Infinity`、`-0` 行为与格式化（`String(x)`、`toFixed`、`toPrecision`、`toExponential`、`toString(radix)`）同 JS。
- 不使用快速数学优化：Android/iOS/Windows 结果逐位一致。

### 4.2 定宽整数（E7 扩展）
- 标注了 `u32` 等类型的值是真正的机器整数：`+ - *` 回绕；`/` 为**截断整除**，`%` 为整数取余；除以 0 是运行时错误。
- 位运算按位宽进行；无符号类型的 `>>` 为逻辑右移，有符号类型 `>>` 为算术右移、`>>>` 为逻辑右移；
  移位位数对 ≤32 位类型取模 32、对 64 位类型取模 64。
- 整数字面量会适配上下文类型并做范围检查（`const x: u8 = 256` 编译错误）。
- 转换函数 `u32(x)`、`i64(x)` 等：从 `number` 截断小数后按 2^n 取模（与 `x >>> 0`、`x | 0` 一致），从其他整数类型/`bigint` 取模。
- 内建：`rotl32 rotr32 rotl64 rotr64 clz32 clz64 ctz32 ctz64 popcnt bswap16 bswap32 bswap64 mulhi64 imulhi64`。
- 在 JS 引擎上（`lib/e7.d.ts` 把它们声明为 `number`）不会回绕；**写成 `u32(a + b)` 的代码在两边结果一致**。

### 4.3 字符串
- `+` 与模板字符串：任意类型按 JS `String(x)` 规则转换后拼接。
- 比较 `< <= > >=` 按 UTF-16 代码单元字典序；`===` 比较内容。
- `s[i]` 越界是运行时错误（JS 返回 `undefined`）。

### 4.4 相等与逻辑
- 只允许 `===` / `!==`；`== null` / `!= null` 作为"是否为 null 或 undefined"的特例允许。
- 比较没有交集的类型（如 `string === number`）是编译错误（同 TS）。
- `&&`、`||` 在条件中可用于任意类型（按真值）；在值位置，`a || b` 返回值（要求类型兼容），`&&` 仅限布尔。
- `??`、`??=`、`?.`（成员、方法调用、函数调用）同 JS。

### 4.5 其他
- `typeof x` 在编译期求值为常量字符串（可空类型不支持）。
- `x as T`：同类型、向上转型、可空去空（运行时检查）、类的向下转型（运行时检查，失败终止）、`JSON.parse(text) as T`（§6.4）。
- `x!` 非空断言：运行时检查，为 null/undefined 时终止。
- 不支持逗号运算符、`void`、`delete`、`in`、`new.target`、标签模板、`eval`。

## 5. 函数与类

- 参数必须有类型注解（箭头函数可由上下文推断）。支持默认参数（可引用前面的参数）、可选参数 `x?: T`、剩余参数 `...xs: T[]`。
  带默认参数的函数不能作为值传递（请包一层箭头函数）。
- 类：字段（可带初始化，类型可推断）、构造函数参数属性（`constructor(public x: number)`）、方法、静态字段/方法、
  单继承 `extends`（派生类构造函数第一条语句必须是 `super(...)`；省略构造函数时自动继承父类构造参数）、
  `super.method()`、`implements`（结构化检查）、`instanceof`。方法覆盖要求签名相同。
  `public/private/protected/readonly` 修饰符被接受但不在运行时强制。
  箭头函数字段可以捕获 `this`。**不支持** getter/setter、抽象类、`#private`、泛型类、类表达式、方法作为值（请用箭头函数）。
- 未初始化的字段初始值为 `0` / `false` / `null`，可选字段为 `undefined`。

## 6. 标准库

与 JavaScript 行为一致（经 Node.js 差分测试验证）的部分：

- **全局**：`parseInt parseFloat isNaN isFinite encodeURIComponent decodeURIComponent encodeURI decodeURI`、
  `Number(x) String(x) Boolean(x) BigInt(x)`、`NaN Infinity`，`console.log/info/warn/error`、`print`、`abort(msg)`。
- **Math**：全部函数与常量（`min/max/hypot` 支持变参与展开），`Math.random()`（宿主可设种子，便于复现）。
- **Number**：`isInteger isSafeInteger isFinite isNaN parseFloat parseInt`、常量；实例 `toString(radix) toFixed toPrecision toExponential`。
- **String**：`length charAt charCodeAt codePointAt at indexOf lastIndexOf includes startsWith endsWith slice substring substr
  toUpperCase toLowerCase trim trimStart trimEnd padStart padEnd repeat split replace replaceAll concat localeCompare`，
  `String.fromCharCode/fromCodePoint`。`split/replace` 只支持字符串参数（无正则）；大小写转换覆盖拉丁、希腊、西里尔字母；
  `localeCompare` 按代码单元比较（不依赖区域设置）。
- **Array**：`length push pop shift unshift at slice splice concat indexOf lastIndexOf includes join reverse fill sort
  map filter forEach reduce reduceRight some every find findIndex findLast findLastIndex`，`Array.from`（可迭代对象或
  `{ length: n }, fn`）、`Array.of`、`new Array<T>(n)`、可写 `length`。默认 `sort()` 按字符串排序（同 JS），排序稳定。
  **越界读取是运行时错误**（JS 返回 `undefined`）；写入下标 `length` 时追加。
- **Map / Set**：`size get set has delete clear forEach keys values entries`，迭代、展开、复制构造。
- **Uint8Array / ArrayBuffer / DataView**：`length byteLength byteOffset buffer set subarray slice fill indexOf lastIndexOf
  includes join reverse at forEach`，`Uint8Array.from/of`，DataView 全部 `get/set{Uint8..Float64,BigInt64,BigUint64}`（大小端）。
- **TextEncoder / TextDecoder**（UTF-8，解码按 WHATWG 规则替换非法序列）。
- **bigint**：全部算术/位运算/比较，`BigInt.asUintN/asIntN`，`toString(radix)`，作为 Map 键。
- **JSON.stringify**（含缩进参数；元组输出为数组；`undefined` 字段省略；循环引用报错）。
- **Object.keys(obj)**、`Date.now()`。

### 6.4 JSON.parse（类型校验）
E7 中必须写成 `JSON.parse(text) as T`，`T` 可以是基本类型、数组、元组、接口/对象类型（嵌套）、可空与可选字段、
`Map<string, V>`（E7 扩展：动态键对象）。解析时**按 T 校验**，缺字段或类型不符时终止并给出路径，例如
`JSON.parse: expected a number at $.items[0].price`。多余字段被忽略。

## 7. 模块

- `import { a, b as c } from "./x"`、`import * as ns from "./x"`、`import type {...}`，`export function/const/class/interface/type/enum`、
  `export { a, b as c }`。路径相对当前文件，可省略扩展名（尝试 `.ts`、`.e7`、`/index.ts`）。禁止循环依赖。
- 内建模块：`e7:crypto`、`e7:encoding`（API 见 `lib/e7.d.ts`）。
- 宿主函数：`declare function name(a: string, n: number): string;`，参数/返回值限于
  `number boolean string 整数 Uint8Array KeyHandle void`，加载模块时与宿主注册的签名比对。
- 入口文件中 `export function` 可被宿主调用（参数与返回值同上的类型才能跨边界传递）。顶层代码在加载时执行一次，
  模块级变量在多次调用之间保持。

## 8. 运行时错误

以下情况终止当前调用（宿主收到错误码与 `文件:行号` 调用栈），VM 本身仍可继续使用：
数组/字符串/字节越界、空引用访问、`!`/`as` 断言失败、整数除零、`throw`/`abort`、JSON 校验失败、
`Map` 严格取值失败、指令预算耗尽（`E7_ERR_BUDGET`，或挂起 `E7_SUSPENDED`）、内存上限（`E7_ERR_MEMORY`）、
调用栈过深（`E7_ERR_STACK`）、宿主函数报错。

## 9. 与 JavaScript 的已知差异（汇总）

| 场景 | JavaScript | E7 |
|---|---|---|
| 数组/字符串越界读取 | `undefined` | 运行时错误 |
| `new Array<T>(n)` 的元素 | 空洞（读出 `undefined`） | `0` / `false` / `null`（引用类型为 `undefined`） |
| 定宽整数运算 | 浮点 | 机器整数回绕 |
| `console.log` 长数组/嵌套对象 | 多行分栏格式 | 单行格式（`JSON.stringify` 完全一致） |
| `JSON.parse` | 返回 `any` | 需要 `as T`，按类型校验 |
| 大小写转换 / `localeCompare` | 完整 Unicode / 区域设置 | 拉丁/希腊/西里尔 / 代码单元比较 |
| Map 迭代中大量删除后插入 | 规范定义的顺序 | 可能因表压缩跳过新元素（极少见） |
| 异常 | `try/catch` | 不支持；`throw` 终止脚本 |
