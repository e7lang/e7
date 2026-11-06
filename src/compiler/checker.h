// Type checker: resolves names, computes types, annotates the AST.
#pragma once
#include <functional>
#include <set>

#include "types.h"

namespace e7 {

struct Program {
  Arena arena;
  Types types;
  std::vector<std::unique_ptr<SourceFile>> files;  // dependency order, entry last
  std::vector<CompileError> errors;
  std::vector<std::unique_ptr<Sym>> syms;
  // results
  std::vector<FuncSym*> funcs;      // every function to compile
  std::vector<ClassSym*> classes;   // classes in layout order (parents first)
  std::vector<VarSym*> globals;
  FuncSym* init = nullptr;          // module initializer (all files' top-level code)
  std::vector<std::pair<std::string, FuncSym*>> exports;
  template <class T>
  T* make() {
    syms.emplace_back(new T());
    return static_cast<T*>(syms.back().get());
  }
};

struct Scope {
  Scope* parent = nullptr;
  FuncSym* fn = nullptr;
  bool fnBoundary = false;
  std::unordered_map<std::string, Sym*> names;
  std::unordered_map<std::string, Type*> narrow;  // path key -> narrowed type (nullptr = none)
};

class Checker {
 public:
  Checker(Program& p, std::function<SourceFile*(const std::string& spec, const std::string& from)> resolve);
  void run();

  // shared helpers (also used by codegen)
  static bool isNullish(Type* t) { return t->t == TT::Null || t->t == TT::Undefined; }
  static bool hasUndefined(Type* t) { return t && (t->t == TT::Undefined || (t->t == TT::Optional && t->hasUndef)); }

 private:
  Program& P;
  Types& T;
  std::function<SourceFile*(const std::string&, const std::string&)> resolveModule;

  // file-level state
  struct FileInfo {
    SourceFile* file;
    Scope* scope;
    std::unordered_map<std::string, Sym*> exports;
  };
  std::unordered_map<std::string, FileInfo*> fileInfo;
  std::vector<std::unique_ptr<FileInfo>> fileInfos;
  std::vector<std::unique_ptr<Scope>> scopePool;
  Scope* globalScope = nullptr;
  FileInfo* curFileInfo = nullptr;
  std::string curFile;

  // function-level state
  Scope* scope = nullptr;
  FuncSym* curFn = nullptr;
  struct LoopCtx { std::string label; bool isLoop; bool isSwitch; };
  std::vector<LoopCtx> loops;
  std::vector<std::string> pendingLabels;

  int recordCounter = 0;

  // assignment targets: a member that is written (1) or read and written (2, compound / ++) is
  // checked with this set, so that accessors resolve to the setter
  Expr* lvalueTarget = nullptr;
  int lvalueMode = 0;
  Type* checkTarget(Expr* target, int mode) {
    Expr* savedT = lvalueTarget;
    int savedM = lvalueMode;
    lvalueTarget = target;
    while (lvalueTarget && lvalueTarget->k == EK::Paren) lvalueTarget = lvalueTarget->a;
    lvalueMode = mode;
    Type* t = check(target, nullptr);
    lvalueTarget = savedT;
    lvalueMode = savedM;
    return t;
  }
  // ---- generics (checker_generic.cpp): monomorphization
  std::vector<std::string> instCtx;  // instantiations being checked ("first<string>"), for messages
  int instCount = 0;                 // instances created (bounded)
  int instNest = 0;                  // instantiation depth of the code being checked
  std::vector<std::unique_ptr<ClassMember>> memberPool;
  Expr* cloneExpr(Expr* e);
  Stmt* cloneStmt(Stmt* s);
  VarDecl cloneDecl(const VarDecl& d);
  void cloneParams(std::vector<Param>& ps);
  ClassMember cloneMember(const ClassMember& m);
  Sym* lookupFrom(Scope* s, const std::string& name);
  GenericSym* newGeneric(GenericSym::Kind k, const std::string& name, std::vector<TypeParam>* params, Loc l);
  Scope* bindTypeParams(GenericSym* g, const std::vector<Type*>& args);
  std::string instanceName(GenericSym* g, const std::vector<Type*>& args);
  int curInstDepth();
  bool instLimits(GenericSym* g, Loc l, const std::string& name);
  bool explicitTypeArgs(GenericSym* g, const std::vector<TypeNode*>& nodes, Loc l, std::vector<Type*>& out);
  int completeTypeArgs(GenericSym* g, std::vector<Type*>& bound, Loc l);
  bool inferCall(GenericSym* g, std::vector<Param>* params, TypeNode* ret, std::vector<Expr*>& args,
                 const std::vector<TypeNode*>& explicitArgs, Type* expected, Loc l, std::vector<Type*>& out);
  void inferMatch(GenericSym* g, std::vector<Type*>& bound, TypeNode* n, Type* t, int depth = 0);
  bool mentionsParams(GenericSym* g, TypeNode* n);
  Type* substPartial(GenericSym* g, const std::vector<Type*>& bound, TypeNode* n);
  FuncSym* instantiateFunc(GenericSym* g, const std::vector<Type*>& args, Loc l);
  ClassSym* instantiateClass(GenericSym* g, const std::vector<Type*>& args, Loc l);
  Type* instantiateType(GenericSym* g, const std::vector<Type*>& args, Loc l);
  Type* genericNamedType(GenericSym* g, const std::vector<TypeNode*>& args, Loc l);
  FuncSym* genericFuncValue(GenericSym* g, Expr* e, Type* expected);
  void staticShadow(ClassSym* c, const std::string& name, VarSym* inherited);
  // object literals with spreads: their properties (per entry, in order) for spreadOrders()
  struct SpreadLit {
    Expr* e;
    std::vector<std::vector<std::pair<std::string, Type*>>> entries;
  };
  std::vector<SpreadLit> spreadLits;
  void spreadOrders();
  Type* checkArrayPatInit(VarDecl& d, Expr* init);
  ClassSym* classOf(Sym* s);  // a class, or the static holder of a generic class

  ClassSym* staticInitClass = nullptr;  // class whose static initializers / blocks are being checked
  ClassSym* lexicalClass();             // class whose body contains the code being checked
  bool checkPrivateAccess(Loc l, const std::string& name, ClassSym* owner);
  Type* checkAccessor(Expr* e, ClassSym* c, const std::string& name, bool isStatic);

  // ---- diagnostics
  void err(Loc l, const std::string& msg);
  Type* errT(Loc l, const std::string& msg) { err(l, msg); return T.unknown(); }
  // "; did you mean 'x'?" suffixes (empty when nothing is close)
  std::string suggestName(const std::string& name, bool typeNames);
  std::string suggestMember(Type* t, const std::string& name);
  std::string suggestNamespaceMember(const std::string& ns, const std::string& name);

  // ---- scopes
  Scope* pushScope(bool fnBoundary = false);
  void popScope();
  void declare(const std::string& name, Sym* s, Loc l);
  Sym* lookup(const std::string& name, bool* crossedFn = nullptr);
  std::string varKey(VarSym* v);
  std::string pathKey(Expr* e, bool* plainConst = nullptr);
  Type* narrowed(const std::string& key, bool plainConst);
  void setNarrow(const std::string& key, Type* t) { if (!key.empty()) scope->narrow[key] = t; }
  void invalidatePath(const std::string& key);
  void markCapture(VarSym* v);

  // ---- declarations
  void declareFile(FileInfo* fi);
  void declareBuiltins();
  FuncSym* newFunc(const std::string& name, std::vector<Param>* params, TypeNode* ret, Stmt* body, Expr* exprBody);
  VarSym* newVar(const std::string& name, Type* type, bool isConst, Loc l);
  void bindImports(FileInfo* fi, Stmt* s);

  // ---- types
  Type* resolveType(TypeNode* n);
  Type* resolveNamedType(const std::string& name, const std::vector<TypeNode*>& args, Loc l);
  void resolveClass(ClassSym* c);
  void resolveInterface(InterfaceSym* s);
  void resolveEnum(EnumSym* e);
  void resolveSig(FuncSym* f);
  Type* globalVarType(VarSym* v);

  // ---- relations
  bool assignable(Type* src, Type* dst);
  bool sameType(Type* a, Type* b) { return a == b; }
  bool structurallyAssignable(Type* src, Type* dst);
  Type* unify(Type* a, Type* b, Loc l, const char* what);
  Type* widen(Type* t);  // literal-ish widening (Null -> stays)
  // Checks expression against target type, inserting conversions. Returns false on error.
  bool coerce(Expr*& e, Type* target, const char* ctx = "assignment");
  Expr* wrapConv(Expr* e, ConvKind k, Type* t);

  // ---- functions & statements
  void checkFunction(FuncSym* f);
  void checkBody(FuncSym* f);
  void checkStmt(Stmt* s);
  void checkBlockStmts(std::vector<Stmt*>& stmts);
  void checkVarDecl(Stmt* s, VarDecl& d, bool isConst, bool topLevel);
  void bindPattern(VarDecl& d, Type* t, bool isConst, bool topLevel, Loc l);
  void hoistFunctions(std::vector<Stmt*>& stmts);
  void checkClassBodies(ClassSym* c);
  bool exits(Stmt* s);
  bool switchCoversEnum(Stmt* s);
  Type* branchEndNarrow(Stmt* b, std::string& key);
  bool assignsPath(Stmt* b, const std::string& key);
  void collectAssigned(Stmt* s, std::set<std::string>& out);
  void collectAssignedExpr(Expr* e, std::set<std::string>& out);
  void invalidateAssigned(Stmt* body, Expr* extra = nullptr);

  // ---- expressions
  Type* check(Expr* e, Type* expected = nullptr);
  Type* checkCond(Expr* e);  // condition context, applies narrowing in scope chain via returned facts
  Type* checkInner(Expr* e, Type* expected);
  Type* checkNum(Expr* e, Type* expected);
  Type* checkIdent(Expr* e, Type* expected = nullptr);
  Type* checkArray(Expr* e, Type* expected);
  Type* checkObject(Expr* e, Type* expected);
  Type* checkUnary(Expr* e, Type* expected);
  Type* checkUpdate(Expr* e);
  Type* checkBinary(Expr* e, Type* expected);
  Type* checkLogical(Expr* e, Type* expected);
  Type* checkAssign(Expr* e);
  Type* checkCondExpr(Expr* e, Type* expected);
  Type* checkCall(Expr* e, Type* expected);
  Type* checkNew(Expr* e, Type* expected);
  Type* checkMember(Expr* e, Type* expected);
  Type* checkIndex(Expr* e);
  Type* checkIndexOn(Expr* e, Type* objectType);
  Type* checkArrow(Expr* e, Type* expected);
  Type* checkTemplate(Expr* e);
  Type* checkAs(Expr* e);
  bool checkLValue(Expr* e);
  std::string memberWriteError(Expr* target);  // "" when the member may be written here
  Type* declaredMemberType(Expr* target);
  std::string exactOptionalError(const std::string& name, Type* declared, Type* value);
  const char* exactOptionalTarget(Expr* target);
  std::string indexWriteError(Expr* target);
  bool spreadExact(const std::string& name, Type* t, bool absentIfUndef, Type* exp, Loc loc);
  void checkArgs(Expr* call, Type* fnType, std::vector<Expr*>& args, size_t skip = 0);
  void expandSpreads(std::vector<Expr*>& args);
  Type* checkBuiltinCall(Expr* e, const std::string& id, Type* expected);
  Type* checkMethodCall(Expr* e, Expr* member, Type* expected);
  Type* checkBuiltinMethod(Expr* e, Expr* member, Type* recvType, Type* expected);
  Type* checkNamespaceMember(Expr* e, NamespaceSym* ns, const std::string& name, bool isCall, Type* expected);
  bool isIntLiteralExpr(Expr* e);
  bool constIntValue(Expr* e, Type* target, int64_t& out, std::string& why);
  bool litFits(Expr* lit, Type* intType, int64_t& out, bool neg, std::string& why);

  // narrowing facts from conditions
  struct Fact { std::string key; Type* t; };
  void joinBranchNarrowing(Stmt* s, const std::vector<Fact>& tf, const std::vector<Fact>& ff);
  void factsFrom(Expr* cond, bool whenTrue, std::vector<Fact>& out);
  void applyFacts(const std::vector<Fact>& facts);

  // builtin helpers
  bool argsMatch(Expr* call, std::vector<Expr*>& args, const std::vector<Type*>& params, size_t minArgs,
                 const char* fname, bool report = true);
  std::string kindSuffix(Type* t);
  Type* elemTypeForIter(Expr* e, Type* t, Loc l);

 public:
  std::vector<ClassSym*> recordClasses;
  ClassSym* tupleRecord(Type* tupleType);
  Type* recordFor(const std::vector<std::pair<std::string, Type*>>& fields, bool tuple = false,
                  const std::vector<std::pair<std::string, Type*>>* methods = nullptr);
};

}  // namespace e7
