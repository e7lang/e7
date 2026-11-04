# e7vm 字节码（.e7b v1）

## 1. 文件格式

所有整数小端。字符串为 `u32 长度 + UTF-8 字节`。

```
magic   "E7B\0"            4 字节
version u32 = 1
flags   u32 = 0
name    str
ints    u32 n, n × i64        整数常量池
nums    u32 n, n × f64        数字常量池（IEEE-754 位模式）
strs    u32 n, n × (u32 len, len × u16)   字符串常量（UTF-16）
sigs    u32 n, n × str        签名，如 "FR>I"：参数 kind 列表 '>' 返回 kind（V = void）
natives u32 n, n × (str name, u32 sig)          原生函数导入（按名字 + 签名链接）
classes u32 n, n × (str name, i32 parent, u8 kind(0 类/1 记录/2 元组),
                    u32 nf × (str name, u8 vt), u32 nm × (u32 func, str methodName))
funcs   u32 n, n × (str name, u32 sig, u16 nparams, u32 nregs × u8 kind, u32 nup × u8 kind,
                    u32 nic × u32 strIdx, u32 ncode × u64, u32 nlines × u32 (pc, line 成对))
globals u32 n, n × (str name, u8 vt)
exports u32 n, n × (str name, u32 func, str hostSig)
init    u32                    模块初始化函数（签名必须是 ">V"）
footer  "E7B$" u32 crc32       对前面全部字节的 CRC32
[签名]  "E7S1" keyId[8] sig[64]  可选：Ed25519 签名（覆盖前面全部字节，含 footer）；keyId = SHA-256(公钥) 前 8 字节
```

- **kind**（寄存器类别）：`0=I`（64 位整数/布尔）、`1=F`（f64）、`2=R`（引用）。每个函数的每个寄存器 kind **固定**。
- **vt**（精确值类型）：`0 number, 1 boolean, 2 i8, 3 u8, 4 i16, 5 u16, 6 i32, 7 u32, 8 i64, 9 u64, 10 ref`。
  用于数组元素、字段、cell、装箱值、Map 键值和格式化。
- **hostSig**：导出函数参数/返回的宿主类型码（见 `e7.h`），宿主据此编解码参数。

## 2. 指令编码

每条指令一个 64 位字：`op:16 | A:16 | B:16 | C:16`，`Bx = B | C<<16`（32 位）。

- 调用类指令后跟参数字：每个字包含 4 个 16 位寄存器号；`CALLV/CALLM/CALLI` 在参数字前还有一个元数据字
  （`CALLV`: 签名索引；`CALLM`: 低 32 位 vtable 槽、高 32 位签名；`CALLI`: 低 32 位内联缓存槽、高 32 位签名）。
- `CLOSURE A B C` 后跟 C 个 16 位捕获说明：最高位为 1 表示取当前闭包的上值 `idx & 0x7FFF`，否则取寄存器（必须持有 cell）。
- 返回值目标为 `0xFFFF` 表示丢弃。
- 融合比较跳转（`JLT_F` 等）的 C 是相对于下一条指令的有符号 16 位偏移；`JMP/JT/JF` 的 Bx 是绝对指令下标。

操作数格式：`I/F/R` 指定 kind 的寄存器，`_` 未使用（必须为 0），`v` 值类型，`n` 任意立即数，`s` 有符号立即数，
`c` 类下标，`e` 字段下标（运行时检查），`Q` 内联缓存槽，`U` 上值下标，`j` 相对跳转；`X*` 表示 A 为 X、Bx 为
`J` 跳转 / `k` 整数常量 / `d` 数字常量 / `t` 字符串常量 / `g` 全局变量 / `m` 32 位立即数；`***` 为特殊编码。

| 指令 | 格式 | 语义 |
|---|---|---|
| `NOP` | `___` | 无操作 |
| `MOV_I` | `II_` | R[A]=R[B] |
| `MOV_F` | `FF_` | R[A]=R[B] |
| `MOV_R` | `RR_` | R[A]=R[B] |
| `LOADI` | `Im*` | R[A]=sext(Bx) |
| `LOADK_I` | `Ik*` | R[A]=ints[Bx] |
| `LOADK_F` | `Fd*` | R[A]=nums[Bx] |
| `LOADK_S` | `Rt*` | R[A]=strs[Bx] |
| `LOADNULL` | `R__` | R[A]=null |
| `LOADUNDEF` | `R__` | R[A]=undefined |
| `GGET_I` | `Ig*` | R[A]=G[Bx] |
| `GGET_F` | `Fg*` | R[A]=G[Bx] |
| `GGET_R` | `Rg*` | R[A]=G[Bx] |
| `GSET_I` | `Ig*` | G[Bx]=R[A] |
| `GSET_F` | `Fg*` | G[Bx]=R[A] |
| `GSET_R` | `Rg*` | G[Bx]=R[A] |
| `ADD_F` | `FFF` | + |
| `SUB_F` | `FFF` | - |
| `MUL_F` | `FFF` | * |
| `DIV_F` | `FFF` | / |
| `MOD_F` | `FFF` | % (JS) |
| `POW_F` | `FFF` | ** (JS) |
| `NEG_F` | `FF_` | -x |
| `BAND_F` | `FFF` | & (ToInt32) |
| `BOR_F` | `FFF` | | |
| `BXOR_F` | `FFF` | ^ |
| `SHL_F` | `FFF` | << |
| `SAR_F` | `FFF` | >> |
| `SHR_F` | `FFF` | >>> |
| `BNOT_F` | `FF_` | ~ |
| `EQ_F` | `IFF` | == |
| `NE_F` | `IFF` | != |
| `LT_F` | `IFF` | < |
| `LE_F` | `IFF` | <= |
| `ADD_I` | `III` | + (mod 2^64) |
| `SUB_I` | `III` | - |
| `MUL_I` | `III` | * |
| `NEG_I` | `II_` | -x |
| `DIVS_I` | `III` | 有符号截断除（除零报错） |
| `DIVU_I` | `III` | 无符号除 |
| `REMS_I` | `III` | 有符号取余 |
| `REMU_I` | `III` | 无符号取余 |
| `AND_I` | `III` | & |
| `OR_I` | `III` | | |
| `XOR_I` | `III` | ^ |
| `NOT_I` | `II_` | ~ |
| `SHL_I` | `III` | << (mod 64) |
| `SAR_I` | `III` | 算术 >> |
| `SHR_I` | `III` | 逻辑 >> |
| `SHL32` | `III` | 32 位 << |
| `SAR32` | `III` | 32 位算术 >> |
| `SHR32` | `III` | 32 位逻辑 >> |
| `ROTL32` | `III` | 循环左移 |
| `ROTR32` | `III` | 循环右移 |
| `ROTL64` | `III` | 循环左移 |
| `ROTR64` | `III` | 循环右移 |
| `CLZ32` | `II_` | 前导零 |
| `CLZ64` | `II_` | 前导零 |
| `CTZ32` | `II_` | 尾随零 |
| `CTZ64` | `II_` | 尾随零 |
| `POPCNT` | `II_` | 1 的个数 |
| `BSWAP16` | `II_` | 字节序反转 |
| `BSWAP32` | `II_` | 字节序反转 |
| `BSWAP64` | `II_` | 字节序反转 |
| `MULHIU` | `III` | 无符号 128 位积高 64 位 |
| `MULHIS` | `III` | 有符号 128 位积高 64 位 |
| `WRAP_I8` | `II_` | 规范化到 i8 |
| `WRAP_U8` | `II_` | 规范化到 u8 |
| `WRAP_I16` | `II_` | 规范化到 i16 |
| `WRAP_U16` | `II_` | 规范化到 u16 |
| `WRAP_I32` | `II_` | 规范化到 i32 |
| `WRAP_U32` | `II_` | 规范化到 u32 |
| `EQ_I` | `III` | == |
| `NE_I` | `III` | != |
| `LT_I` | `III` | 有符号 < |
| `LE_I` | `III` | 有符号 <= |
| `LTU_I` | `III` | 无符号 < |
| `LEU_I` | `III` | 无符号 <= |
| `ADDI_I` | `IIs` | R[A]=R[B]+sext(C) |
| `NOT_B` | `II_` | 逻辑非 |
| `I2F` | `FI_` | 有符号整数→number |
| `U2F` | `FI_` | 无符号整数→number |
| `F2I` | `IF_` | number→int64（截断，模 2^64） |
| `F2B` | `IF_` | number 真值 |
| `JMP` | `_J*` | 跳转到 Bx |
| `JT` | `IJ*` | R[A]≠0 时跳转 |
| `JF` | `IJ*` | R[A]=0 时跳转 |
| `JLT_F` | `FFj` | R[A]<R[B] 时相对跳转 C |
| `JNLT_F` | `FFj` | !(R[A]<R[B]) |
| `JLE_F` | `FFj` | R[A]<=R[B] |
| `JNLE_F` | `FFj` | !(R[A]<=R[B]) |
| `JEQ_F` | `FFj` | == |
| `JNE_F` | `FFj` | != |
| `JLT_I` | `IIj` | 有符号 < |
| `JNLT_I` | `IIj` | !(<) |
| `JLE_I` | `IIj` | <= |
| `JNLE_I` | `IIj` | !(<=) |
| `JEQ_I` | `IIj` | == |
| `JNE_I` | `IIj` | != |
| `JNULLISH` | `RJ*` | null/undefined 时跳转 |
| `JNOTNULLISH` | `RJ*` | 非空时跳转 |
| `ISNULL` | `IR_` | === null |
| `ISUNDEF` | `IR_` | === undefined |
| `ISNULLISH` | `IR_` | == null |
| `RET_I` | `I__` | 返回 |
| `RET_F` | `F__` | 返回 |
| `RET_R` | `R__` | 返回 |
| `RET_V` | `___` | 返回 void |
| `CALL` | `***` | 直接调用函数 B |
| `CALLV` | `***` | 调用闭包 R[B]（运行时检查签名） |
| `CALLM` | `***` | 虚方法调用（vtable 槽） |
| `CALLI` | `***` | 接口方法调用（按名字，内联缓存） |
| `CALLN` | `***` | 调用原生函数 B |
| `CLOSURE` | `***` | 创建闭包，捕获 cell |
| `NEWCELL` | `Rv_` | 新建 cell（被捕获变量） |
| `CGET_I` | `IR_` | 读 cell |
| `CGET_F` | `FR_` | 读 cell |
| `CGET_R` | `RR_` | 读 cell |
| `CSET_I` | `RI_` | 写 cell |
| `CSET_F` | `RF_` | 写 cell |
| `CSET_R` | `RR_` | 写 cell |
| `UGET_I` | `IU_` | 读上值 |
| `UGET_F` | `FU_` | 读上值 |
| `UGET_R` | `RU_` | 读上值 |
| `USET_I` | `UI_` | 写上值 |
| `USET_F` | `UF_` | 写上值 |
| `USET_R` | `UR_` | 写上值 |
| `UCELL` | `RU_` | 取上值 cell 本身 |
| `NEW` | `Rc_` | 新建类/记录实例 |
| `GETF_I` | `IRe` | 读字段 C |
| `GETF_F` | `FRe` | 读字段 |
| `GETF_R` | `RRe` | 读字段 |
| `SETF_I` | `ReI` | 写字段 B |
| `SETF_F` | `ReF` | 写字段 |
| `SETF_R` | `ReR` | 写字段 |
| `GETN_I` | `IRQ` | 按名字读属性（IC） |
| `GETN_F` | `FRQ` | 按名字读 |
| `GETN_R` | `RRQ` | 按名字读 |
| `SETN_I` | `RQI` | 按名字写 |
| `SETN_F` | `RQF` | 按名字写 |
| `SETN_R` | `RQR` | 按名字写 |
| `INSTOF` | `IRc` | instanceof |
| `BOX_I` | `RIv` | 装箱 |
| `BOX_F` | `RF_` | 装箱 |
| `UNBOX_I` | `IR_` | 拆箱（空则报错） |
| `UNBOX_F` | `FR_` | 拆箱 |
| `CHKNN` | `R__` | 非空检查 |
| `NEWARR` | `Rvn` | 新数组（元素类型 B，容量 C） |
| `AGET_I` | `IRF` | a[number] |
| `AGET_F` | `FRF` | a[number] |
| `AGET_R` | `RRF` | a[number] |
| `AGETI_I` | `IRI` | a[int] |
| `AGETI_F` | `FRI` | a[int] |
| `AGETI_R` | `RRI` | a[int] |
| `ASET_I` | `RFI` | a[number]=v（下标=length 时追加） |
| `ASET_F` | `RFF` | a[number]=v |
| `ASET_R` | `RFR` | a[number]=v |
| `ASETI_I` | `RII` | a[int]=v |
| `ASETI_F` | `RIF` | a[int]=v |
| `ASETI_R` | `RIR` | a[int]=v |
| `APUSH_I` | `RI_` | push |
| `APUSH_F` | `RF_` | push |
| `APUSH_R` | `RR_` | push |
| `ALEN` | `FR_` | length（number） |
| `ALENI` | `IR_` | length（int） |
| `BGET` | `FRF` | bytes[number] |
| `BGETI` | `IRI` | bytes[int] |
| `BSET` | `RFF` | bytes[number]=ToUint8(v) |
| `BSETI` | `RII` | bytes[int]=v |
| `BLEN` | `FR_` | length |
| `BLENI` | `IR_` | length（int） |
| `SLEN` | `FR_` | 字符串长度 |
| `SCAT` | `RRR` | 字符串拼接 |
| `SEQ` | `IRR` | 字符串 === |
| `SNE` | `IRR` | !== |
| `SLT` | `IRR` | < |
| `SLE` | `IRR` | <= |
| `NUM2STR` | `RF_` | String(number) |
| `INT2STR` | `RIv` | 整数→字符串（C 为值类型） |
| `BOOL2STR` | `RI_` | String(boolean) |
| `STRTRUTHY` | `IR_` | 字符串真值 |
| `EQ_R` | `IRR` | 引用相等 |
| `NE_R` | `IRR` | 引用不等 |
| `ABORT` | `R__` | 以 R[A] 为消息终止 |

## 3. 验证规则（加载时，`src/core/verify.cpp`）

1. 结构：所有计数有上界检查，任何越界读取都会使加载失败；CRC32 校验；版本检查。
2. 签名字符串合法；每个函数参数 kind 与签名一致；初始化函数签名为 `>V`。
3. 每条指令：操作码合法；每个寄存器操作数存在且 **kind 与指令要求一致**；常量/全局/类/函数/原生/内联缓存/上值下标在范围内；
   全局与上值的 kind 与指令一致；未使用字段为 0。
4. 调用：参数个数与目标签名一致、每个参数寄存器 kind 匹配、返回目标 kind 匹配（void 调用不能有目标）；
   间接调用（闭包/方法/接口）在运行时再次比对签名 id。
5. 跳转目标必须是指令起始位置；函数最后一条指令必须是无条件转移（`RET_*`、`JMP`、`ABORT`），不能"掉出"函数。
6. 类：父类下标小于自身（无环）；继承的字段布局与 vtable 签名一致；方法第一个参数为接收者。

由于寄存器 kind 固定，验证是单遍线性的。验证保证"整数永远不会被当作指针使用"；对象形状（字段数量与 kind、数组元素类型、
字符串/字节类型）在运行时由解释器检查。因此即使字节码被恶意构造，也只会导致加载失败或干净的运行时错误，不会破坏内存
（`tests/fuzz_load.cpp` 持续验证这一点）。

## 4. 执行模型

- 每次调用分配一个寄存器窗口（栈槽连续，引用寄存器在入口清零）；脚本之间的调用不使用 C 栈（可挂起）。
- 垃圾回收：精确标记-清除。根为调用帧中的引用寄存器、全局变量、常量字符串、被固定的对象。只在安全点（循环回边、调用）或
  内存紧张时回收；原生函数执行期间不回收。
- 指令预算：每条回边按循环体长度扣减，每次调用扣 1；耗尽时报错或挂起。
