# e7vm bytecode (.e7b format 2; format 3 planned)

## 1. File format

All integers are little-endian. A string is `u32 length + UTF-8 bytes`.

```
magic   "E7B\0"            4 bytes
version u32 = 2            bytecode format (e7 0.2.x)
flags   u32                bit 0: the module carries debug information (informational; readers ignore unknown bits)
requires u32               minimum e7vm version: major << 16 | minor << 8 | patch (see "Compatibility")
name    str
ints    u32 n, n × i64        integer constant pool
nums    u32 n, n × f64        number constant pool (IEEE-754 bit patterns)
strs    u32 n, n × (u32 len, len × u16)   string constants (UTF-16)
sigs    u32 n, n × str        signatures, e.g. "FR>I": param kind list '>' return kind (V = void)
natives u32 n, n × (str name, u32 sig)          native function imports (linked by name + signature)
classes u32 n, n × (str name, i32 parent, u8 kind(0 class/1 record/2 tuple),
                    u32 nf × (str name, u8 vt), u32 nm × (u32 func, str methodName))
funcs   u32 n, n × (str name, u32 sig, u16 nparams, u32 nregs × u8 kind, u32 nup × u8 kind,
                    u32 nic × u32 strIdx, u32 ncode × u64,
                    u32 nhandlers × (u32 start, u32 end, u32 handler, u32 reg),
                    u32 nlines × u32 (pc, line pairs))
globals u32 n, n × (str name, u8 vt)
exports u32 n, n × (str name, u32 func, str hostSig)
init    u32                    module init function (signature must be ">V")
footer  "E7B$" u32 crc32       CRC32 of all preceding bytes
[sig]   "E7S1" keyId[8] sig[64]  optional: Ed25519 signature (covers all preceding bytes, incl. footer); keyId = first 8 bytes of SHA-256(public key)
```

Detached signatures of other files (update manifests; `e7_sign_detached`, `e7c --sign-file`) are Ed25519 over
`"e7-detached-v1\0" || file bytes`, stored separately (`<file>.sig`, 128 hex digits); the prefix keeps them
distinct from module signatures. See [DEPLOYMENT.md](DEPLOYMENT.md#3-manifests).

- **line table**: `(pc, line)` pairs sorted by pc (a frame's line is that of the last pair with `pc <= ip`). A function
  whose code comes from a source file other than the entry file (imported modules, and module initialization in
  multi-file programs) appends an extension after the pairs: `(0xFFFFFFFF, 0x53454C46)` followed by
  `(0xFFFFFFFF, pc), (0xFFFFFFFF, strIdx)` entries meaning "from `pc` on, the source file is string constant `strIdx`"
  (a path relative to the entry file). Readers that stop at the first pair whose pc is past the instruction ignore it,
  so older VMs still load such modules; single-file programs carry no extension.
- **debug information** (optional, `e7c --debug`; see [DEBUGGING.md](DEBUGGING.md)): after the pairs and the file
  segments (if any), a function may append `(0xFFFFFFFF, 0x31474244 "DBG1")`, a word count `n` (< 2^31), `n` payload
  words, and one `0` padding word if needed to keep the table a list of pairs. Older readers ignore it (their pair
  scan stops at the first marker and their file-segment scan at the first group that is not
  `(marker, pc, marker, index)`). Payload (all `u32`):
  ```
  npos,   npos  x (pc, line, col)     position of the code from pc on; pcs strictly increasing
  nstmt,  nstmt x (pc, line, col)     statement boundaries (instruction starts, strictly increasing) and their position
  nlocal, nlocal x (reg, startPc, endPc, vt | flags << 8, name)
                                      a variable (or parameter) held in register reg while startPc <= pc < endPc;
                                      flags: 1 = the register holds a cell (captured variable), 2 = parameter
  nup,    nup x name                  names of the function's upvalues (0 or one per upvalue)
  name = u32 byte length (<= 4096), then the UTF-8 bytes packed little-endian into ceil(length / 4) words
  ```
  Columns are 1-based UTF-8 byte offsets. Debug information does not change the code.
- **kind** (register kind): `0=I` (64-bit integer/boolean), `1=F` (f64), `2=R` (reference). Each register of each function has a **fixed** kind.
- **vt** (exact value type): `0 number, 1 boolean, 2 i8, 3 u8, 4 i16, 5 u16, 6 i32, 7 u32, 8 i64, 9 u64, 10 ref`.
  Used for array elements, fields, cells, boxed values, Map keys/values, and formatting.
- **hostSig**: host type codes for an exported function's parameters/return (see `e7.h`); the host uses them to encode/decode arguments.

## 2. Instruction encoding

Each instruction is one 64-bit word: `op:16 | A:16 | B:16 | C:16`, `Bx = B | C<<16` (32 bits).

- Call instructions are followed by argument words: each word holds 4 16-bit register numbers; `CALLV/CALLM/CALLI` have one metadata word before the argument words
  (`CALLV`: signature index; `CALLM`: low 32 bits vtable slot, high 32 bits signature; `CALLI`: low 32 bits inline cache slot, high 32 bits signature).
- `CLOSURE A B C` is followed by C 16-bit capture descriptors: if the top bit is 1, take upvalue `idx & 0x7FFF` of the current closure; otherwise take a register (which must hold a cell).
- A return value destination of `0xFFFF` means the value is discarded.
- For fused compare-and-jump instructions (`JLT_F` etc.), C is a signed 16-bit offset relative to the next instruction; for `JMP/JT/JF`, Bx is an absolute instruction index.

Operand formats: `I/F/R` register of the given kind, `_` unused (must be 0), `v` value type, `n` any immediate, `s` signed immediate,
`c` class index, `e` field index (checked at runtime), `Q` inline cache slot, `U` upvalue index, `j` relative jump; `X*` means A is X and Bx is a
`J` jump / `k` integer constant / `d` number constant / `t` string constant / `g` global variable / `m` 32-bit immediate; `***` is a special encoding.

| Instruction | Format | Semantics |
|---|---|---|
| `NOP` | `___` | No operation |
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
| `DIVS_I` | `III` | Signed truncating division (error on division by zero) |
| `DIVU_I` | `III` | Unsigned division |
| `REMS_I` | `III` | Signed remainder |
| `REMU_I` | `III` | Unsigned remainder |
| `AND_I` | `III` | & |
| `OR_I` | `III` | | |
| `XOR_I` | `III` | ^ |
| `NOT_I` | `II_` | ~ |
| `SHL_I` | `III` | << (mod 64) |
| `SAR_I` | `III` | Arithmetic >> |
| `SHR_I` | `III` | Logical >> |
| `SHL32` | `III` | 32-bit << |
| `SAR32` | `III` | 32-bit arithmetic >> |
| `SHR32` | `III` | 32-bit logical >> |
| `ROTL32` | `III` | Rotate left |
| `ROTR32` | `III` | Rotate right |
| `ROTL64` | `III` | Rotate left |
| `ROTR64` | `III` | Rotate right |
| `CLZ32` | `II_` | Leading zeros |
| `CLZ64` | `II_` | Leading zeros |
| `CTZ32` | `II_` | Trailing zeros |
| `CTZ64` | `II_` | Trailing zeros |
| `POPCNT` | `II_` | Count of 1 bits |
| `BSWAP16` | `II_` | Byte swap |
| `BSWAP32` | `II_` | Byte swap |
| `BSWAP64` | `II_` | Byte swap |
| `MULHIU` | `III` | High 64 bits of unsigned 128-bit product |
| `MULHIS` | `III` | High 64 bits of signed 128-bit product |
| `WRAP_I8` | `II_` | Normalize to i8 |
| `WRAP_U8` | `II_` | Normalize to u8 |
| `WRAP_I16` | `II_` | Normalize to i16 |
| `WRAP_U16` | `II_` | Normalize to u16 |
| `WRAP_I32` | `II_` | Normalize to i32 |
| `WRAP_U32` | `II_` | Normalize to u32 |
| `EQ_I` | `III` | == |
| `NE_I` | `III` | != |
| `LT_I` | `III` | Signed < |
| `LE_I` | `III` | Signed <= |
| `LTU_I` | `III` | Unsigned < |
| `LEU_I` | `III` | Unsigned <= |
| `ADDI_I` | `IIs` | R[A]=R[B]+sext(C) |
| `NOT_B` | `II_` | Logical not |
| `I2F` | `FI_` | Signed integer→number |
| `U2F` | `FI_` | Unsigned integer→number |
| `F2I` | `IF_` | number→int64 (truncate, mod 2^64) |
| `F2B` | `IF_` | number truthiness |
| `JMP` | `_J*` | Jump to Bx |
| `JT` | `IJ*` | Jump if R[A]≠0 |
| `JF` | `IJ*` | Jump if R[A]=0 |
| `JLT_F` | `FFj` | Relative jump by C if R[A]<R[B] |
| `JNLT_F` | `FFj` | !(R[A]<R[B]) |
| `JLE_F` | `FFj` | R[A]<=R[B] |
| `JNLE_F` | `FFj` | !(R[A]<=R[B]) |
| `JEQ_F` | `FFj` | == |
| `JNE_F` | `FFj` | != |
| `JLT_I` | `IIj` | Signed < |
| `JNLT_I` | `IIj` | !(<) |
| `JLE_I` | `IIj` | <= |
| `JNLE_I` | `IIj` | !(<=) |
| `JEQ_I` | `IIj` | == |
| `JNE_I` | `IIj` | != |
| `JNULLISH` | `RJ*` | Jump if null/undefined |
| `JNOTNULLISH` | `RJ*` | Jump if not nullish |
| `ISNULL` | `IR_` | === null |
| `ISUNDEF` | `IR_` | === undefined |
| `ISNULLISH` | `IR_` | == null |
| `RET_I` | `I__` | Return |
| `RET_F` | `F__` | Return |
| `RET_R` | `R__` | Return |
| `RET_V` | `___` | Return void |
| `CALL` | `***` | Direct call to function B |
| `CALLV` | `***` | Call closure R[B] (signature checked at runtime) |
| `CALLM` | `***` | Virtual method call (vtable slot) |
| `CALLI` | `***` | Interface method call (by name, inline cache) |
| `CALLN` | `***` | Call native function B |
| `CLOSURE` | `***` | Create closure, capturing cells |
| `NEWCELL` | `Rv_` | New cell (captured variable) |
| `CGET_I` | `IR_` | Read cell |
| `CGET_F` | `FR_` | Read cell |
| `CGET_R` | `RR_` | Read cell |
| `CSET_I` | `RI_` | Write cell |
| `CSET_F` | `RF_` | Write cell |
| `CSET_R` | `RR_` | Write cell |
| `UGET_I` | `IU_` | Read upvalue |
| `UGET_F` | `FU_` | Read upvalue |
| `UGET_R` | `RU_` | Read upvalue |
| `USET_I` | `UI_` | Write upvalue |
| `USET_F` | `UF_` | Write upvalue |
| `USET_R` | `UR_` | Write upvalue |
| `UCELL` | `RU_` | Get the upvalue cell itself |
| `NEW` | `Rc_` | New class/record instance |
| `GETF_I` | `IRe` | Read field C |
| `GETF_F` | `FRe` | Read field |
| `GETF_R` | `RRe` | Read field |
| `SETF_I` | `ReI` | Write field B |
| `SETF_F` | `ReF` | Write field |
| `SETF_R` | `ReR` | Write field |
| `GETN_I` | `IRQ` | Read property by name (IC) |
| `GETN_F` | `FRQ` | Read by name |
| `GETN_R` | `RRQ` | Read by name |
| `SETN_I` | `RQI` | Write by name |
| `SETN_F` | `RQF` | Write by name |
| `SETN_R` | `RQR` | Write by name |
| `INSTOF` | `IRc` | instanceof |
| `BOX_I` | `RIv` | Box |
| `BOX_F` | `RF_` | Box |
| `UNBOX_I` | `IR_` | Unbox (error if null) |
| `UNBOX_F` | `FR_` | Unbox |
| `CHKNN` | `R__` | Non-null check |
| `NEWARR` | `Rvn` | New array (element type B, capacity C) |
| `AGET_I` | `IRF` | a[number] |
| `AGET_F` | `FRF` | a[number] |
| `AGET_R` | `RRF` | a[number] |
| `AGETI_I` | `IRI` | a[int] |
| `AGETI_F` | `FRI` | a[int] |
| `AGETI_R` | `RRI` | a[int] |
| `ASET_I` | `RFI` | a[number]=v (appends when index=length) |
| `ASET_F` | `RFF` | a[number]=v |
| `ASET_R` | `RFR` | a[number]=v |
| `ASETI_I` | `RII` | a[int]=v |
| `ASETI_F` | `RIF` | a[int]=v |
| `ASETI_R` | `RIR` | a[int]=v |
| `APUSH_I` | `RI_` | push |
| `APUSH_F` | `RF_` | push |
| `APUSH_R` | `RR_` | push |
| `ALEN` | `FR_` | length (number) |
| `ALENI` | `IR_` | length (int) |
| `BGET` | `FRF` | bytes[number] |
| `BGETI` | `IRI` | bytes[int] |
| `BSET` | `RFF` | bytes[number]=ToUint8(v) |
| `BSETI` | `RII` | bytes[int]=v |
| `BLEN` | `FR_` | length |
| `BLENI` | `IR_` | length (int) |
| `SLEN` | `FR_` | String length |
| `SCAT` | `RRR` | String concatenation |
| `SEQ` | `IRR` | String === |
| `SNE` | `IRR` | !== |
| `SLT` | `IRR` | < |
| `SLE` | `IRR` | <= |
| `NUM2STR` | `RF_` | String(number) |
| `INT2STR` | `RIv` | Integer→string (C is the value type) |
| `BOOL2STR` | `RI_` | String(boolean) |
| `STRTRUTHY` | `IR_` | String truthiness |
| `EQ_R` | `IRR` | Reference equality |
| `NE_R` | `IRR` | Reference inequality |
| `ABORT` | `R__` | Abort with R[A] as the message |
| `DEBUGGER` | `___` | `debugger;` statement: no-op, a stop when a debugger is attached |
| `THROW` | `R__` | Throw the Error object R[A] (catchable; see the handler table) |

`ABORT` is never catchable. `THROW` and the standard-library errors that carry a JavaScript error name
(`"RangeError: ..."`) are: when one is raised, the VM looks for the innermost frame (of the current entry from the
host or from a native callback) whose handler range `[start, end)` contains that frame's current instruction, drops
the frames above it, stores the Error object (an array `[name, message]` of two strings) in register `reg` and
continues at `handler`. Handlers are listed innermost first.

## 3. Verification rules (at load time, `src/core/verify.cpp`)

1. Structure: all counts are bounds-checked, and any out-of-bounds read fails the load; CRC32 check; version check.
2. Signature strings are well-formed; each function's parameter kinds match its signature; the init function's signature is `>V`.
3. Each instruction: the opcode is valid; every register operand exists and its **kind matches what the instruction requires**; constant/global/class/function/native/inline cache/upvalue indices are in range;
   global and upvalue kinds match the instruction; unused fields are 0.
4. Calls: the argument count matches the target signature, each argument register kind matches, and the return destination kind matches (a void call cannot have a destination);
   indirect calls (closure/method/interface) compare the signature id again at runtime.
5. Jump targets must be instruction start positions; the last instruction of a function must be an unconditional transfer (`RET_*`, `JMP`, `ABORT`, `THROW`), so execution cannot "fall off" the end of a function.
   Handler tables: `start < end <= code length`, `start`, `end` (unless it is the code length) and `handler` at
   instruction starts, `reg` a reference register, at most 65535 entries, and every handler listed after the ones it
   encloses, with ranges either nested or disjoint.
6. Classes: the parent class index is less than the class's own index (no cycles); inherited field layouts and vtable signatures are consistent; the first parameter of a method is the receiver.
7. Debug information: a malformed extension (counts past the payload, names over 4096 bytes, trailing words) fails the load; positions and statement
   boundaries must be strictly increasing and inside the code (statements at instruction starts); every variable's register exists and has the kind of
   its value type (reference kind when it holds a cell), its range lies within the code, its value type and flags are valid; upvalue names are absent
   or one per upvalue. The debugger reads registers only through this mapping and checks object types at run time.

Because register kinds are fixed, verification is a single linear pass. Verification guarantees that "an integer is never used as a pointer"; object shapes (field count and kinds, array element types,
string/byte types) are checked by the interpreter at runtime. So even maliciously crafted bytecode can only cause a load failure or a clean runtime error, never memory corruption
(`tests/fuzz_load.cpp` continuously verifies this).

## 4. Execution model

- Each call allocates a register window (contiguous stack slots, reference registers zeroed on entry); calls between scripts do not use the C stack (so they can be suspended).
- Garbage collection: precise mark-sweep. Roots are reference registers in call frames, global variables, constant strings, and pinned objects. Collection happens only at safepoints (loop back edges, calls) or
  under memory pressure; no collection happens while a native function is running.
- Instruction budget: each back edge deducts the loop body length, and each call deducts 1; on exhaustion the VM reports an error or suspends.

## 5. Compatibility

- The `version` field is the bytecode format. e7 0.2.x reads and writes format 2 only; a module of another format is
  rejected at load time with `E7_ERR_VERSION` (0.1 modules must be recompiled).
- `requires` is the oldest e7vm that can run the module. The compiler computes it from the instructions and native
  functions the module uses (each has a "since" version, `src/core/since.h`), and from features that change how a
  VM must treat it: a class (not a record) with a field named `#...` (a `#private` field, hidden from
  `Object.keys`, `JSON.stringify` and `console.log` since e7vm 0.2.5) requires 0.2.5, a record with accessors
  (section 6.5) 0.3.1; a VM older than `requires` rejects the
  module with `E7_ERR_VERSION` instead of failing verification. `e7c --target 0.2` makes newer features compile
  errors, so a server can build modules for the VM versions of the apps in the field.
- `e7_inspect()` (and `e7c --info`) read the format, `requires` and signature without loading a module.
- Promise: within a minor release line (0.2.x), every VM runs every module compiled by any 0.2.x compiler, with
  the same results. `tests/compat/<version>/` keeps modules compiled by released compilers; CI checks that the 0.1
  ones are rejected with `E7_ERR_VERSION` and that the 0.2.x ones load and print the recorded output.

## 6. Format 3 (0.3)

Format 3 is format 2 with the additions below ([DESIGN-0.3.md](DESIGN-0.3.md), D1-D4 and D7); everything not
mentioned is unchanged. The compiler writes format 2 when a module uses none of them (and `e7c --target 0.2`
compiles records without presence masks), and a 0.3 VM reads both formats. A 0.2.x VM refuses a format-3 module
with `E7_ERR_VERSION` ("e7b format 3 is not supported") before reading its body.

### 6.1 File format changes

```
version u32 = 3
strs    ... (unchanged)
regexps u32 n, n x (str source, str flags)       new, after the natives: regular expression literals (D4)
classes u32 n, n x (str name, i32 parent, u8 kind, u8 cflags, ...)      cflags new
funcs   u32 n, n x (str name, u32 sig, u16 nparams, u8 fflags, ...)    fflags new
```

- **regexps**: the source (UTF-8) and flags (a subset of `dgimsuy`, each at most once) of each regular expression
  literal. The VM compiles every entry when the module is loaded; a pattern that does not compile fails the load
  (`E7_ERR_LOAD`).
- **cflags**, bit 0: the instances carry a presence mask (D7): one bit per field after the fields, all set by `NEW`;
  allowed for records only (`kind` 1). Bit 1 (0.3.1): a record with accessor properties (section 6.5). Other bits
  are 0.
- **fflags**: bits 0-1 the function kind (0 plain, 1 generator, 2 `async`, 3 `async` generator, since 0.3.1);
  bits 2-3 the kind of the yielded value, bits 4-5 the kind of the value `next(v)` sends, bits 6-7 the kind of the
  value the body returns (generators and async generators: none or the yielded kind; `async` functions: the
  promise's value), each 0 none, 1 `I`, 2 `F`, 3 `R`. A plain function has fflags 0.

### 6.2 New instructions (`since 0.3.0`)

| Instruction | Format | Semantics |
|---|---|---|
| `GEN_START` | `___` | First instruction of a generator: create its coroutine, save the frame (arguments in place), return the coroutine to the caller |
| `ASYNC_START` | `v__` | First instruction of an `async` function: create its coroutine (owning this frame) and its promise, of value type `A`; the body continues |
| `YIELD` | `***` | `A` yielded value (yield kind), `B` received value (next kind, or `0xFFFF`), `C` mode (`I`); one following word: bits 0-15 the register receiving `return(v)`'s value (return kind, or `0xFFFF`), the rest 0. Saves the frame and gives `A` to the resumer with `done = false`; on resumption `B` receives the sent value and `C` the mode (0 `next`, 2 `return`); `throw(e)` raises `e` at this instruction |
| `RESUME_I` / `RESUME_F` / `RESUME_R` | `***` | `A` coroutine (`R`), `B` destination of the value (kind of the variant), `C` destination of the done flag (`I`); one following word: bits 0-15 the register with the sent value (`0xFFFF`: none), bits 16-17 the mode (0 `next`, 1 `throw`, 2 `return`), bits 18-19 the sent value's kind (0 none, 1 `I`, 2 `F`, 3 `R`), the rest 0 (0.3.1: bits 20-44 of an async generator request, below) |
| `AWAIT` | `***` | `A` destination (any kind, or `0xFFFF`), `B` promise (`R`), `C` 0; one word. Suspends the `async` function until the promise settles; then `A` receives the value, or the rejection is raised at this instruction. Leaving the frame gives the caller the function's promise (the first await) or ends the microtask |
| `FIELD_ABSENT` | `Re_` | Clear the presence bit of field `B` of the record in `A` (D7); `SETF` / `SETN` set it again |

`RET_*` from a generator's frame finishes the coroutine (`done = true` with the value). From an `async` function's
frame it resolves the promise; `RET_R` of a promise (also in a function whose promise has another value kind)
adopts it (`PromiseResolveThenableJob`). An error that leaves an `async` frame rejects its promise instead of
propagating to the caller. The module initializer may be `async` (top-level `await`): signature `>R` with `async`
fflags. The host signature of an exported `async` function has the promise's value as its return code; the call
returns that value once the promise settles (or `E7_PENDING` while host tickets are open).

**Async generators (0.3.1).** A function of kind 3 starts with `GEN_START` (its call returns the coroutine without
running the body) and may contain `YIELD` and `AWAIT`. Its `next(v)` / `throw(e)` / `return(v)` are `RESUME_R` with
bit 20 of the extra word set (an *async request*):

| Extra word of `RESUME_R` | Meaning |
|---|---|
| bits 0-19 | as for every `RESUME` (sent register, mode, sent kind) |
| bit 20 | 1: a request to an async generator; `B` receives the request's promise (`C` is set to 0) |
| bits 21-36 | the class of the IteratorResult record the promise is resolved with: a record `{ value, done }` whose `done` field is a boolean |
| bits 37-44 | the value type of the yielded type `T` (a primitive value in a reference `value` field is boxed with it) |
| bits 45-63 | 0 |

The VM keeps the request queue of the ECMAScript specification in the coroutine. A request gets its promise at
once; `next` on a finished generator resolves it with `{ value: undefined, done: true }`, `throw` on a generator that
has not started or has finished rejects it, `return(v)` on such a generator awaits `v` (then resolves). Otherwise the
request is queued, and the body runs (its frame is pushed as for `RESUME`, but nothing is written to the
resumer's registers when it leaves) only if the generator waits at its start or at a `YIELD`; a generator that
runs or waits at an `AWAIT` answers the request later. In an async generator's frame:

- `YIELD` resolves the first request with `{ value: A, done: false }`; if another request is queued, the body
  continues at once with that request's value (`B`, the return register and the mode `C` as for a resumption, or
  the error raised at the `YIELD`), otherwise the frame is saved and popped. The compiler awaits the yielded value
  before the `YIELD` (`yield v` awaits `v`) and, for mode 2, awaits the return value after it.
- `AWAIT` suspends the frame like in an `async` function (the caller gets nothing).
- `RET_*` completes the generator: the first request gets `{ value, done: true }`, then the requests queued after it
  are answered (`next`: `{ value: undefined, done: true }`, `throw`: rejected, `return(v)`: after awaiting `v`).
  An error that leaves the frame rejects the first request's promise instead, and the frame's caller continues.

The records are instances of the class the request names (a primitive field gets `NaN` / `0` where JavaScript has
`undefined`). The generator's kinds are checked against the request's at run time (`generator type mismatch`), as for
`RESUME`.

Promise, iterator, regular expression and typed array operations are native functions with `since 0.3.0`
(`Promise.*`, `TA.*`, `RegExp.*`, `Array.toSorted` / `toReversed` / `toSpliced` / `with*`, `Date.set`,
`Error.newCause` / `newAggregate` / `cause` / `errors`, `JSON.parseRec` / `tryParseRec`, `Object.has`,
`Global.structuredClone`, `test.later*`); they need no new verification rules.

### 6.3 Verification (additions)

1. `GEN_START` is the first instruction of every generator and async generator function and appears nowhere
   else; the same for `ASYNC_START` in `async` functions, whose operand must be a value type of the fflags' return
   kind (`R` when none). A coroutine's signature returns `R`.
2. `YIELD` appears only in generators and async generators, `AWAIT` only in `async` functions and async
   generators; `YIELD`'s registers have the kinds in `fflags` (`B` and the return register only when that kind is
   not none), `C` is `I`.
3. `RESUME_*`: `A` is `R`, `B` has the variant's kind, `C` is `I`; the sent register exists exactly when its kind
   is not none and has that kind; mode 3 is invalid and mode 1 (`throw`) needs an `R` value. Bits 20-63 of the extra
   word are 0, except in an async request (0.3.1: `RESUME_R` only, a record class `{ value, done: boolean }` whose
   `value` field is a reference or has the kind of the value type in bits 37-44, bits 45-63 0). The coroutine's kinds
   are compared with the variant and the sent value **at run time** (the register is an untyped reference): a
   mismatch, a resumption of a running coroutine or of a non-coroutine is a runtime error, as for the signature
   check of `CALLV`.
4. `RET_*` in a coroutine has the fflags' return kind (also `RET_R` in an `async` function, checked at run time).
5. `FIELD_ABSENT`: `A` is `R`; the class's presence flag and the field index are checked at run time.
6. Handler tables, jump targets and register kinds are checked as in format 2; a resumption continues after the
   `YIELD` / `AWAIT` with the same register kinds, so no new data-flow rules are needed.

### 6.4 Compatibility

- A format-3 module has `requires` 0.3.0 or later; a 0.2.x VM rejects it with `E7_ERR_VERSION` before reading the
  body (forward-compatibility CI job). A module with an async generator (function kind 3) or an async request
  (`RESUME_R` with bit 20) has `requires` 0.3.1: a 0.3.0 VM rejects it with `E7_ERR_VERSION`, and
  `e7c --target 0.3` refuses it.
- The 0.3 promise extends section 5's: every 0.3.x VM runs every module compiled by a 0.2.x or 0.3.x compiler (the
  backward-compatibility CI job runs the suite compiled by the 0.2.6 compiler on the current VM).

### 6.5 Records with accessors (`since 0.3.1`)

Getters and setters in object literals (SPEC section 5) use no new instruction or native function: a record whose
literal has accessors carries **cflags bit 1** (`kClassFlagAccessors`, records only). In such a record, field `x`
is an accessor property when the vtable has a method named `get x` and / or `set x`; the field holds the
**accessor pair**, a tuple (kind 2) of three fields: the getter closure (`() => T`, or null), the setter closure
(`(v: T) => void`, or null) and a slot that is never written, whose value type is the property's. The methods
`get x` / `set x` (the targets of `CALLI` by name) load the pair and call its closure. Natives that read
properties treat the field as JavaScript treats an accessor property: `Object.keys` lists it, `JSON.stringify`
(also `console.log`'s `%j`) and `e7:test`'s `assertEqual` call the getter (a closure with no parameters whose return
kind is the slot's; anything else is the runtime error "malformed accessor property"), `console.log` prints
`[Getter]`, `[Setter]` or `[Getter/Setter]`, and `structuredClone` refuses it (a runtime error; the compiler
rejects types with accessors).

- Verification: the flag only on records (`kind` 1); a field named by an accessor method is a reference field.
  The accessor pair is checked when a native calls the getter (above).
- `requires` is 0.3.1 for a module with the flag (`kRecordAccessorsSince` in `src/core/since.h`); a 0.3.0 VM
  rejects it with `E7_ERR_VERSION`, and `e7c --target 0.3.0` refuses the program ("the program uses getters and
  setters in object literals, which needs e7vm 0.3.1"). Modules that do not use the feature compile to the same
  bytes as before and run on 0.3.0.
