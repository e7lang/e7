// AST for the E7 language (strict TypeScript subset).
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "../core/common.h"

namespace e7 {

struct Type;
struct VarSym;
struct FuncSym;
struct ClassSym;
struct GenericSym;
struct Expr;

struct Loc {
  uint32_t line = 0, col = 0;
};

// ------------------------------------------------------------------ type syntax
enum class TNK { Ref, Array, Tuple, Func, Object, Union, Literal };
struct TypeNode {
  TNK k;
  Loc loc;
  std::string name;                  // Ref
  std::vector<TypeNode*> args;       // Ref type args / Tuple elems / Union members
  TypeNode* elem = nullptr;          // Array
  bool readonly = false;             // `readonly T[]` / `readonly [A, B]`
  struct Param { std::string name; TypeNode* type; bool optional = false; bool rest = false; };
  std::vector<Param> params;         // Func
  TypeNode* ret = nullptr;           // Func
  // accessor: 0 = property or method, 1 = `get name(): T`, 2 = `set name(v: T)` (interfaces / type literals)
  struct Member { std::string name; TypeNode* type; bool optional = false; bool method = false; bool readonly = false; int accessor = 0; Expr* computed = nullptr; };
  std::vector<Member> members;       // Object
  std::string literal;               // Literal (string literal type / null / undefined handled as Ref)
  // a return type `x is T` (Ref "boolean") / `asserts x is T` / `asserts x` (Ref "void"): the parameter
  // and the type it is narrowed to (null for `asserts x`)
  std::string predParam;
  TypeNode* predType = nullptr;
  bool asserts = false;
  bool numLiteral = false;           // Literal: a numeric literal type (value in `num`)
  double num = 0;
};

// A type parameter <T extends C = D>
struct TypeParam {
  std::string name;
  TypeNode* constraint = nullptr;
  TypeNode* def = nullptr;
  Loc loc;
};

// ------------------------------------------------------------------ expressions
enum class EK {
  Num, BigLit, Str, Template, Bool, Null, Undefined, Ident, This, Super,
  Array, Object, Unary, Update, Binary, Logical, Assign, Cond, Call, New,
  Member, Index, NonNull, As, Arrow, Spread, Conv, Paren, TypeOf,
  Yield,  // a = operand (or null); delegate: yield*
  Await,  // a = operand
  RegExp  // a regular expression literal: raw = the pattern as written, regexFlags
};

struct Stmt;
struct Expr;
struct VarDecl;

struct Param {
  std::string name;
  TypeNode* type = nullptr;
  Expr* init = nullptr;
  bool optional = false;
  bool rest = false;
  bool isProperty = false;  // constructor parameter property
  bool readonly = false;    // `readonly` parameter property
  Loc loc;
  VarSym* sym = nullptr;
  VarDecl* pattern = nullptr;  // destructuring parameter
  Type* innerType = nullptr;   // arrow parameter with a default: the type inside the body (the
                               // closure's parameter type is that type | undefined)
};

enum class ConvKind {
  None, IntLitToInt, NumToBox, IntToBox, ToOptional, EnumToNum, EnumToStr, Unbox, ClassUpcast,
  IntToNum, FuncAdapt
};

struct ObjProp {
  std::string key;
  Expr* value = nullptr;
  bool shorthand = false;
  bool spread = false;
  bool method = false;  // method shorthand `name(params) { body }` (value is an Arrow)
  int accessor = 0;     // 1 = `get name() { body }`, 2 = `set name(v) { body }` (value is an Arrow; own 'this')
  bool coverInit = false;  // `{ a = 1 }`: only valid as a destructuring assignment target (value is an Assign)
  Expr* computed = nullptr;  // `{ [K]: v }`: the key expression (a constant known at compile time)
  Loc loc;
};

struct Expr {
  EK k;
  Loc loc;
  Loc nameLoc;           // Member: position of the property name
  Type* type = nullptr;  // filled by checker
  Type* innerType = nullptr;  // optional chaining: type of the member/call result before '?.' widening
  bool chained = false;       // a '.' / '[' link after a '?.' of the same chain: short-circuited with it
  // literal data
  double num = 0;
  bool isIntLit = false;     // Num: written as integer literal
  std::string raw;           // Num raw text / BigLit digits / Ident name / Member name / op
  std::u16string str;        // Str
  std::string regexFlags;    // RegExp
  bool bval = false;
  // template
  std::vector<std::u16string> quasis;
  std::vector<std::u16string> rawQuasis;  // Template: the raw text of the parts (String.raw / tagged templates)
  bool tagged = false;                    // Call: a tagged template (list[0] is the template)
  // children
  Expr* a = nullptr;  // unary operand, binary lhs, callee, object, cond test
  Expr* b = nullptr;  // binary rhs, index, cond consequent
  Expr* c = nullptr;  // cond alternate
  std::vector<Expr*> list;  // array elements, call args, template exprs
  std::vector<ObjProp> props;
  std::vector<int> fieldSrc;  // object literal with spreads: prop index supplying each record field (-1 = absent)
  std::vector<std::vector<int>> fieldCond;  // later optional spread sources: override only when not undefined
  // object literal with spreads: the property order follows the run-time order of spread source
  // `spreadAltSrc` (an object type): when that value is an instance of spreadAltTest[i] (OP_INSTOF), the
  // result is a spreadAltCls[i] (the same fields in another order); otherwise a `cls`
  int spreadAltSrc = -1;
  std::vector<ClassSym*> spreadAltTest, spreadAltCls;
  std::vector<TypeNode*> typeArgs;
  TypeNode* asType = nullptr;
  bool prefix = false;    // update
  bool optional = false;  // ?. member / call
  bool isGenerator = false, isAsync = false;  // Arrow (function expression / method): function* / async
  bool delegate = false;  // Yield: yield*
  // arrow function
  std::vector<TypeParam> typeParams;  // generic arrow function <T>(x: T) => ...
  std::vector<Param> params;
  TypeNode* retType = nullptr;
  Stmt* body = nullptr;       // block body
  Expr* exprBody = nullptr;   // expression body
  FuncSym* fn = nullptr;      // arrow/function expression symbol
  // resolution (checker)
  VarSym* var = nullptr;
  FuncSym* callee = nullptr;  // direct function / method target
  ClassSym* cls = nullptr;
  int builtin = -1;           // builtin member / function id
  int field = -1;             // resolved field index
  int vslot = -1;             // virtual method slot
  int intrinsic = 0;          // special lowering tag
  ConvKind conv = ConvKind::None;
  std::string native;         // native function to call
  int64_t ival = 0;           // folded integer constant (IntLitToInt)
};

// ------------------------------------------------------------------ statements
enum class SK {
  Var, Func, Class, Interface, TypeAlias, Enum, Expr, Block, If, While, DoWhile, For, ForOf, ForIn,
  Return, Break, Continue, Switch, Labeled, Throw, Empty, Import, Export, Declare, Debugger,
  Try  // body = try block, els = catch block (or null), then = finally block (or null),
       // decls[0] = catch variable (if any)
};

struct VarDecl {
  // pattern: simple name, or array / object destructuring
  std::string name;
  std::vector<VarDecl> elems;   // array pattern (empty name = hole) / object pattern
  std::vector<std::string> keys;  // object pattern property names ("" for a rest element)
  bool isArrayPat = false, isObjPat = false;
  bool isRest = false;          // `...name` element of an array or object pattern (always last)
  TypeNode* type = nullptr;
  Expr* init = nullptr;         // initializer; for an element of a pattern, its default value
  Type* restType = nullptr;     // rest element: type of the collected value (checker)
  GenericSym* generic = nullptr;  // `const f = <T>(...) => ...`: the generic function (no variable)
  Loc loc;
  VarSym* sym = nullptr;
};

struct ClassMember {
  enum Kind { Field, Method, Ctor, StaticBlock } kind = Field;
  std::string name;  // "#name" for private members
  bool isStatic = false;
  bool readonly = false;
  bool optional = false;
  bool isAbstract = false;
  int accessor = 0;  // Method: 1 = getter, 2 = setter
  bool isGenerator = false, isAsync = false;  // Method: *name() / async name()
  bool isDeclare = false;                     // Field: `declare x: T` (no initializer)
  Expr* computed = nullptr;                   // `[K]` member name: a constant known at compile time
  std::vector<TypeParam> typeParams;  // generic method
  TypeNode* type = nullptr;
  Expr* init = nullptr;
  std::vector<Param> params;
  TypeNode* retType = nullptr;
  Stmt* body = nullptr;
  Loc loc;
  FuncSym* fn = nullptr;
};

struct SwitchCase {
  Expr* test = nullptr;  // null = default
  std::vector<Stmt*> body;
};

struct ImportSpec {
  std::string name, alias;  // import: alias is the local name; export: alias is the exported name ("" = name)
  Loc loc;
  bool typeOnly = false;    // `import { type T }`: usable as a type only
};

struct EnumMember {
  std::string name;
  Expr* init = nullptr;
};

struct Stmt {
  SK k;
  Loc loc;
  bool exported = false;
  bool typeOnly = false;           // `import type ...` / `export type ... from`: erased, does not run the module
  bool isDefault = false;          // `export default`: Func / Class / Interface (exported as "default"), or
                                   // Var: `export default <expression>` (a hidden const named "default")
  bool isConst = false;            // Var
  bool isGenerator = false, isAsync = false;  // Func: function* / async function
  bool isAwait = false;            // ForOf: for await
  std::vector<VarDecl> decls;      // Var
  std::string name;                // Func / Class / Interface / TypeAlias / Enum / labels
  std::vector<TypeParam> typeParams;
  std::vector<Param> params;       // Func
  TypeNode* retType = nullptr;     // Func
  Stmt* body = nullptr;            // Func body, loop body, labeled body
  std::vector<Stmt*> stmts;        // Block
  Expr* expr = nullptr;            // Expr / Return / Throw / If test / loop test / switch disc
  Stmt* then = nullptr;
  Stmt* els = nullptr;
  Stmt* init = nullptr;            // For init (Var or Expr stmt)
  Expr* update = nullptr;          // For update
  std::string label;               // Break/Continue target
  std::vector<SwitchCase> cases;
  // class
  bool isAbstract = false;
  TypeNode* extends = nullptr;
  std::vector<TypeNode*> implements;
  std::vector<ClassMember> members;
  // interface
  std::vector<TypeNode*> ifaceExtends;
  TypeNode* typeBody = nullptr;    // interface (as object type) or alias target
  // enum
  std::vector<EnumMember> enumMembers;
  bool isConstEnum = false;
  // import / export list / re-export
  std::vector<ImportSpec> imports;
  std::string from;     // Import: the module; Export: `export ... from` (a re-export) when not empty
  std::string nsAlias;  // import * as X / export * as X from
  bool starExport = false;  // export * from
  // resolution
  FuncSym* fn = nullptr;
  ClassSym* cls = nullptr;
  GenericSym* generic = nullptr;  // generic function / class / interface / alias declaration
  int declareKind = 0;  // Declare: 1 = function
};

// Owns all nodes of one compilation.
struct Arena {
  std::vector<std::unique_ptr<Expr>> exprs;
  std::vector<std::unique_ptr<Stmt>> stmts;
  std::vector<std::unique_ptr<TypeNode>> types;
  std::vector<std::unique_ptr<VarDecl>> decls;
  Expr* expr(EK k, Loc l) { exprs.emplace_back(new Expr()); exprs.back()->k = k; exprs.back()->loc = l; return exprs.back().get(); }
  Stmt* stmt(SK k, Loc l) { stmts.emplace_back(new Stmt()); stmts.back()->k = k; stmts.back()->loc = l; return stmts.back().get(); }
  VarDecl* decl(const VarDecl& d) { decls.emplace_back(new VarDecl(d)); return decls.back().get(); }
  TypeNode* type(TNK k, Loc l) { types.emplace_back(new TypeNode()); types.back()->k = k; types.back()->loc = l; return types.back().get(); }
};

struct SourceFile {
  std::string path;   // normalized path used for imports
  std::string text;
  bool evaluated = true;  // false: reached only through `import type` / `export type`, so never run (as in JS)
  std::vector<Stmt*> body;
};

struct CompileError {
  std::string file;
  Loc loc;
  std::string msg;
};

}  // namespace e7
