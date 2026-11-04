// Lexer and recursive-descent parser for the E7 language (strict TypeScript subset).
#include "parser.h"

#include "../core/numfmt.h"

#include <cmath>
#include <cstdlib>
#include <unordered_set>

namespace e7 {

namespace {

enum class TK { Eof, Ident, Num, BigInt, Str, TmplFull, TmplHead, TmplMid, TmplTail, Punct, PrivateName };

struct Tok {
  TK k = TK::Eof;
  std::string text;      // identifier / punctuator / number raw
  std::u16string str;    // string / template cooked value
  double num = 0;
  bool isInt = false;
  bool nl = false;       // newline before this token
  Loc loc;
};

struct ParseError {
  Loc loc;
  std::string msg;
};

// ------------------------------------------------------------------ lexer
class Lexer {
 public:
  Lexer(const std::string& src) : s(src) {}
  std::vector<Tok> run() {
    std::vector<Tok> out;
    std::vector<int> tmplDepth;  // brace depth stack for template substitutions
    int braceDepth = 0;
    while (true) {
      bool nl = skipSpace();
      Tok t;
      t.nl = nl;
      t.loc = loc();
      if (p >= s.size()) { t.k = TK::Eof; out.push_back(t); break; }
      char c = s[p];
      if (c == '`') {
        p++; col++;
        lexTemplate(t, true);
        if (t.k == TK::TmplHead) tmplDepth.push_back(braceDepth);
        out.push_back(t);
        continue;
      }
      if (c == '}' && !tmplDepth.empty() && tmplDepth.back() == braceDepth) {
        p++; col++;
        lexTemplate(t, false);
        if (t.k == TK::TmplTail) tmplDepth.pop_back();
        out.push_back(t);
        continue;
      }
      if (isIdStart(c) || (unsigned char)c >= 0x80) { lexIdent(t); out.push_back(t); continue; }
      if (c == '#' && p + 1 < s.size() && isIdStart(s[p + 1])) {
        p++; col++;
        lexIdent(t);
        t.k = TK::PrivateName;
        out.push_back(t);
        continue;
      }
      if (isdigit((unsigned char)c) || (c == '.' && p + 1 < s.size() && isdigit((unsigned char)s[p + 1]))) {
        lexNumber(t);
        out.push_back(t);
        continue;
      }
      if (c == '"' || c == '\'') { lexString(t, c); out.push_back(t); continue; }
      lexPunct(t);
      if (t.text == "{") braceDepth++;
      else if (t.text == "}") braceDepth--;
      out.push_back(t);
    }
    return out;
  }

 private:
  const std::string& s;
  size_t p = 0;
  uint32_t line = 1, col = 1;

  Loc loc() const { return Loc{line, col}; }
  [[noreturn]] void fail(const std::string& m) { throw ParseError{loc(), m}; }
  static bool isIdStart(char c) { return isalpha((unsigned char)c) || c == '_' || c == '$'; }
  static bool isIdPart(char c) { return isalnum((unsigned char)c) || c == '_' || c == '$'; }

  bool skipSpace() {
    bool nl = false;
    while (p < s.size()) {
      char c = s[p];
      if (c == '\n') { nl = true; p++; line++; col = 1; }
      else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') { p++; col++; }
      else if (c == '/' && p + 1 < s.size() && s[p + 1] == '/') {
        while (p < s.size() && s[p] != '\n') p++;
      } else if (c == '/' && p + 1 < s.size() && s[p + 1] == '*') {
        p += 2; col += 2;
        bool closed = false;
        while (p + 1 < s.size()) {
          if (s[p] == '*' && s[p + 1] == '/') { p += 2; col += 2; closed = true; break; }
          if (s[p] == '\n') { nl = true; line++; col = 1; } else col++;
          p++;
        }
        if (!closed) fail("unterminated comment");
      } else if ((unsigned char)c == 0xEF && p + 2 < s.size() && (unsigned char)s[p + 1] == 0xBB && (unsigned char)s[p + 2] == 0xBF) {
        p += 3;  // BOM
      } else break;
    }
    return nl;
  }

  void lexIdent(Tok& t) {
    t.k = TK::Ident;
    size_t b = p;
    while (p < s.size() && (isIdPart(s[p]) || (unsigned char)s[p] >= 0x80)) { p++; col++; }
    t.text = s.substr(b, p - b);
    for (unsigned char ch : t.text)
      if (ch >= 0x80) fail("non-ASCII identifiers are not supported");
  }

  void lexNumber(Tok& t) {
    size_t b = p;
    t.k = TK::Num;
    auto digitsOf = [&](int radix) {
      std::string d;
      while (p < s.size()) {
        char c = s[p];
        if (c == '_') { p++; col++; continue; }
        int v = isdigit((unsigned char)c) ? c - '0' : isalpha((unsigned char)c) ? tolower(c) - 'a' + 10 : 99;
        if (v >= radix) break;
        d.push_back(c);
        p++; col++;
      }
      return d;
    };
    if (s[p] == '0' && p + 1 < s.size() && strchr("xXoObB", s[p + 1])) {
      char r = (char)tolower(s[p + 1]);
      int radix = r == 'x' ? 16 : r == 'o' ? 8 : 2;
      p += 2; col += 2;
      std::string d = digitsOf(radix);
      if (d.empty()) fail("invalid numeric literal");
      if (p < s.size() && s[p] == 'n') {
        p++; col++;
        t.k = TK::BigInt;
        t.text = std::string("0") + r + d;
        return;
      }
      double v = 0;
      for (char c : d) v = v * radix + (isdigit((unsigned char)c) ? c - '0' : tolower(c) - 'a' + 10);
      t.num = v;
      t.isInt = true;
      t.text = std::string("0") + r + d;
      if (p < s.size() && isIdPart(s[p])) fail("identifier directly after number");
      return;
    }
    std::string d = digitsOf(10);
    bool isInt = true;
    if (p < s.size() && s[p] == 'n') {
      p++; col++;
      t.k = TK::BigInt;
      t.text = d;
      return;
    }
    if (d.size() > 1 && d[0] == '0') fail("legacy octal literals are not allowed");
    std::string txt = d;
    if (p < s.size() && s[p] == '.') {
      isInt = false;
      p++; col++;
      txt += "." + digitsOf(10);
    }
    if (p < s.size() && (s[p] == 'e' || s[p] == 'E')) {
      isInt = false;
      txt += 'e';
      p++; col++;
      if (p < s.size() && (s[p] == '+' || s[p] == '-')) { txt += s[p]; p++; col++; }
      std::string e = digitsOf(10);
      if (e.empty()) fail("invalid exponent");
      txt += e;
    }
    if (p < s.size() && isIdPart(s[p])) fail("identifier directly after number");
    if (txt.empty() || txt == ".") fail("invalid number");
    t.num = parseDoubleC(txt);
    t.isInt = isInt;
    t.text = s.substr(b, p - b);
    // normalized digits without separators
    t.text = txt;
  }

  void appendCp(std::u16string& out, uint32_t cp) {
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back((char16_t)(0xD800 + (cp >> 10)));
      out.push_back((char16_t)(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back((char16_t)cp);
    }
  }

  int hexv(char c) {
    if (isdigit((unsigned char)c)) return c - '0';
    c = (char)tolower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  }

  // Handles an escape sequence after '\'. Returns false at line continuation.
  void lexEscape(std::u16string& out) {
    if (p >= s.size()) fail("unterminated string");
    char c = s[p++];
    col++;
    switch (c) {
      case 'n': out.push_back('\n'); break;
      case 't': out.push_back('\t'); break;
      case 'r': out.push_back('\r'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'v': out.push_back('\v'); break;
      case '0':
        if (p < s.size() && isdigit((unsigned char)s[p])) fail("octal escapes are not allowed");
        out.push_back(0);
        break;
      case 'x': {
        if (p + 1 >= s.size() || hexv(s[p]) < 0 || hexv(s[p + 1]) < 0) fail("invalid \\x escape");
        out.push_back((char16_t)(hexv(s[p]) * 16 + hexv(s[p + 1])));
        p += 2; col += 2;
        break;
      }
      case 'u': {
        uint32_t cp = 0;
        if (p < s.size() && s[p] == '{') {
          p++; col++;
          int n = 0;
          while (p < s.size() && s[p] != '}') {
            int h = hexv(s[p]);
            if (h < 0) fail("invalid \\u escape");
            cp = cp * 16 + h;
            if (cp > 0x10FFFF) fail("code point out of range");
            p++; col++; n++;
          }
          if (p >= s.size() || n == 0) fail("invalid \\u escape");
          p++; col++;
        } else {
          for (int i = 0; i < 4; i++) {
            if (p >= s.size() || hexv(s[p]) < 0) fail("invalid \\u escape");
            cp = cp * 16 + hexv(s[p]);
            p++; col++;
          }
        }
        appendCp(out, cp);
        break;
      }
      case '\r':
        if (p < s.size() && s[p] == '\n') p++;
        line++; col = 1;
        break;
      case '\n':
        line++; col = 1;
        break;
      default: {
        // UTF-8 sequence or plain char escapes to itself
        p--; col--;
        appendUtf8Char(out);
      }
    }
  }

  void appendUtf8Char(std::u16string& out) {
    unsigned char c = (unsigned char)s[p];
    size_t n = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
    if (p + n > s.size()) n = s.size() - p;
    std::u16string u = utf8ToUtf16(s.data() + p, n);
    out += u;
    p += n;
    col++;
  }

  void lexString(Tok& t, char q) {
    t.k = TK::Str;
    p++; col++;
    while (true) {
      if (p >= s.size() || s[p] == '\n') fail("unterminated string literal");
      char c = s[p];
      if (c == q) { p++; col++; break; }
      if (c == '\\') { p++; col++; lexEscape(t.str); continue; }
      appendUtf8Char(t.str);
    }
  }

  void lexTemplate(Tok& t, bool start) {
    while (true) {
      if (p >= s.size()) fail("unterminated template literal");
      char c = s[p];
      if (c == '`') {
        p++; col++;
        t.k = start ? TK::TmplFull : TK::TmplTail;
        return;
      }
      if (c == '$' && p + 1 < s.size() && s[p + 1] == '{') {
        p += 2; col += 2;
        t.k = start ? TK::TmplHead : TK::TmplMid;
        return;
      }
      if (c == '\\') { p++; col++; lexEscape(t.str); continue; }
      if (c == '\r') { p++; if (p < s.size() && s[p] == '\n') p++; t.str.push_back('\n'); line++; col = 1; continue; }
      if (c == '\n') { p++; t.str.push_back('\n'); line++; col = 1; continue; }
      appendUtf8Char(t.str);
    }
  }

  void lexPunct(Tok& t) {
    static const char* puncts[] = {
        ">>>=", "...", "===", "!==", "**=", "<<=", ">>=", ">>>", "&&=", "||=", "??=",
        "=>", "==", "!=", "<=", ">=", "&&", "||", "??", "?.", "++", "--", "+=", "-=", "*=", "/=", "%=",
        "&=", "|=", "^=", "<<", ">>", "**",
        "{", "}", "(", ")", "[", "]", ";", ",", "<", ">", "+", "-", "*", "/", "%", "&", "|", "^",
        "!", "~", "?", ":", "=", ".", "@"};
    t.k = TK::Punct;
    for (const char* pu : puncts) {
      size_t n = strlen(pu);
      if (s.compare(p, n, pu) == 0) {
        // "?." followed by a digit is "?" then ".5"
        if (n == 2 && pu[0] == '?' && pu[1] == '.' && p + 2 < s.size() && isdigit((unsigned char)s[p + 2])) continue;
        t.text = pu;
        p += n;
        col += (uint32_t)n;
        return;
      }
    }
    fail(std::string("unexpected character '") + s[p] + "'");
  }
};

// ------------------------------------------------------------------ parser
class Parser {
 public:
  Parser(SourceFile& f, Arena& a, std::vector<Tok>&& toks) : file(f), A(a), t(std::move(toks)) {}

  void parseProgram() {
    while (!isEof()) file.body.push_back(parseStatement(true));
  }

 private:
  SourceFile& file;
  Arena& A;
  std::vector<Tok> t;
  size_t i = 0;
  int noIn = 0;
  int depth = 0;
  // Bounds recursion so that hostile input cannot exhaust the native stack (mobile threads are small).
  struct Nest {
    Parser& p;
    explicit Nest(Parser& pp) : p(pp) {
      if (++p.depth > kMaxNesting) p.fail("program is nested too deeply");
    }
    ~Nest() { p.depth--; }
  };
  static constexpr int kMaxNesting = 160;

  const Tok& cur() const { return t[i]; }
  const Tok& peek(size_t k = 1) const { return t[i + k < t.size() ? i + k : t.size() - 1]; }
  bool isEof() const { return cur().k == TK::Eof; }
  Loc loc() const { return cur().loc; }
  [[noreturn]] void fail(const std::string& m) { throw ParseError{loc(), m}; }
  [[noreturn]] void failAt(Loc l, const std::string& m) { throw ParseError{l, m}; }

  bool is(const char* p) const { return (cur().k == TK::Punct || cur().k == TK::Ident) && cur().text == p; }
  bool isP(const char* p) const { return cur().k == TK::Punct && cur().text == p; }
  bool isKw(const char* k) const { return cur().k == TK::Ident && cur().text == k; }
  bool eatP(const char* p) { if (isP(p)) { i++; return true; } return false; }
  bool eatKw(const char* k) { if (isKw(k)) { i++; return true; } return false; }
  void expectP(const char* p) {
    if (!eatP(p)) fail(std::string("expected '") + p + "' but found '" + describe(cur()) + "'");
  }
  void expectKw(const char* k) {
    if (!eatKw(k)) fail(std::string("expected '") + k + "'");
  }
  static std::string describe(const Tok& tk) {
    switch (tk.k) {
      case TK::Eof: return "end of file";
      case TK::Str: return "string";
      case TK::Num: return tk.text;
      default: return tk.text;
    }
  }
  static bool isReserved(const std::string& w) {
    static const std::unordered_set<std::string> r = {
        "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do",
        "else", "enum", "export", "extends", "false", "finally", "for", "function", "if", "import", "in",
        "instanceof", "new", "null", "return", "super", "switch", "this", "throw", "true", "try",
        "typeof", "var", "void", "while", "with", "let", "yield", "await"};
    return r.count(w) > 0;
  }
  std::string ident() {
    if (cur().k != TK::Ident) fail("expected identifier but found '" + describe(cur()) + "'");
    if (isReserved(cur().text)) fail("'" + cur().text + "' is a reserved word");
    return t[i++].text;
  }
  std::string propName() {
    // identifiers including reserved words, strings, numbers
    if (cur().k == TK::Ident) return t[i++].text;
    if (cur().k == TK::Str) return utf16ToUtf8(t[i++].str);
    if (cur().k == TK::Num) return t[i++].text;
    if (cur().k == TK::PrivateName) fail("private names (#x) are not supported; use 'private'");
    fail("expected property name");
  }
  void semi() {
    if (eatP(";")) return;
    if (isP("}") || isEof() || cur().nl) return;
    fail("expected ';' but found '" + describe(cur()) + "'");
  }

  // ---------------------------------------------------------------- types
  TypeNode* parseType() {
    Nest guard(*this);
    Loc l = loc();
    eatP("|");
    TypeNode* first = parseTypeNoUnion();
    if (!isP("|")) return first;
    TypeNode* u = A.type(TNK::Union, l);
    u->args.push_back(first);
    while (eatP("|")) u->args.push_back(parseTypeNoUnion());
    return u;
  }
  TypeNode* parseTypeNoUnion() {
    TypeNode* ty = parsePrimaryType();
    while (isP("[") && peek().k == TK::Punct && peek().text == "]" && !cur().nl) {
      Loc l = loc();
      i += 2;
      TypeNode* arr = A.type(TNK::Array, l);
      arr->elem = ty;
      ty = arr;
    }
    if (isP("&")) fail("intersection types are not supported");
    return ty;
  }
  bool looksLikeFunctionType() {
    // at '(' : function type if matching ')' is followed by '=>'
    size_t j = i;
    int depth = 0;
    for (; j < t.size(); j++) {
      if (t[j].k == TK::Punct) {
        if (t[j].text == "(") depth++;
        else if (t[j].text == ")") { if (--depth == 0) break; }
      }
      if (t[j].k == TK::Eof) return false;
    }
    return j + 1 < t.size() && t[j + 1].k == TK::Punct && t[j + 1].text == "=>";
  }
  TypeNode* parsePrimaryType() {
    Loc l = loc();
    if (isKw("readonly")) { i++; return parseTypeNoUnion(); }
    if (isKw("keyof") || isKw("typeof") || isKw("infer") || isKw("unique"))
      fail("'" + cur().text + "' types are not supported");
    if (isP("(")) {
      if (looksLikeFunctionType()) {
        TypeNode* f = A.type(TNK::Func, l);
        expectP("(");
        while (!isP(")")) {
          TypeNode::Param pr;
          if (eatP("...")) pr.rest = true;
          pr.name = ident();
          if (eatP("?")) pr.optional = true;
          expectP(":");
          pr.type = parseType();
          f->params.push_back(pr);
          if (!eatP(",")) break;
        }
        expectP(")");
        expectP("=>");
        f->ret = parseType();
        return f;
      }
      i++;
      TypeNode* inner = parseType();
      expectP(")");
      return inner;
    }
    if (isP("[")) {
      i++;
      TypeNode* tu = A.type(TNK::Tuple, l);
      while (!isP("]")) {
        if (cur().k == TK::Ident && peek().k == TK::Punct && peek().text == ":") i += 2;  // labeled tuple
        tu->args.push_back(parseType());
        if (!eatP(",")) break;
      }
      expectP("]");
      return tu;
    }
    if (isP("{")) return parseObjectType();
    if (cur().k == TK::Str) {
      TypeNode* lt = A.type(TNK::Literal, l);
      lt->literal = utf16ToUtf8(t[i++].str);
      return lt;
    }
    if (cur().k == TK::Num || isP("-")) fail("numeric literal types are not supported");
    if (cur().k != TK::Ident) fail("expected type");
    TypeNode* r = A.type(TNK::Ref, l);
    r->name = t[i++].text;
    while (isP(".") && peek().k == TK::Ident) {  // qualified names e.g. crypto.KeyHandle
      i++;
      r->name += "." + t[i++].text;
    }
    if (isP("<") && !cur().nl) {
      i++;
      while (!isP(">")) {
        r->args.push_back(parseType());
        if (!eatP(",")) break;
      }
      closeAngle();
    }
    return r;
  }
  void closeAngle() {
    // handle '>>' and '>>>' tokens closing nested generics
    if (isP(">")) { i++; return; }
    if (isP(">>") || isP(">>>") || isP(">=") || isP(">>=")) {
      t[i].text = t[i].text.substr(1);
      t[i].loc.col++;
      return;
    }
    fail("expected '>'");
  }
  TypeNode* parseObjectType() {
    Loc l = loc();
    expectP("{");
    TypeNode* o = A.type(TNK::Object, l);
    while (!isP("}")) {
      TypeNode::Member m;
      if (isKw("readonly") && peek().k == TK::Ident) { i++; m.readonly = true; }
      if (isP("[")) fail("index signatures are not supported; use Map<K, V>");
      m.name = propName();
      if (eatP("?")) m.optional = true;
      if (isP("(")) {
        // method signature
        TypeNode* f = A.type(TNK::Func, loc());
        expectP("(");
        while (!isP(")")) {
          TypeNode::Param pr;
          if (eatP("...")) pr.rest = true;
          pr.name = ident();
          if (eatP("?")) pr.optional = true;
          expectP(":");
          pr.type = parseType();
          f->params.push_back(pr);
          if (!eatP(",")) break;
        }
        expectP(")");
        expectP(":");
        f->ret = parseType();
        m.type = f;
        m.method = true;
      } else {
        expectP(":");
        m.type = parseType();
      }
      o->members.push_back(m);
      if (!eatP(";") && !eatP(",")) {
        if (!isP("}") && !cur().nl) fail("expected ';' in type literal");
      }
    }
    expectP("}");
    return o;
  }

  // ---------------------------------------------------------------- statements
  Stmt* parseBlock() {
    Stmt* b = A.stmt(SK::Block, loc());
    expectP("{");
    while (!isP("}")) {
      if (isEof()) fail("unexpected end of file, expected '}'");
      b->stmts.push_back(parseStatement(false));
    }
    expectP("}");
    return b;
  }

  Stmt* parseStatement(bool topLevel) {
    Nest guard(*this);
    Loc l = loc();
    if (cur().k == TK::Punct) {
      if (isP("{")) return parseBlock();
      if (eatP(";")) return A.stmt(SK::Empty, l);
      if (isP("@")) fail("decorators are not supported");
    }
    if (cur().k == TK::Ident) {
      const std::string& w = cur().text;
      if (w == "export") {
        if (!topLevel) fail("export is only allowed at top level");
        i++;
        if (isKw("default")) fail("default exports are not supported; use named exports");
        if (isP("{")) {
          // export { a, b }
          Stmt* s = A.stmt(SK::Export, l);
          i++;
          while (!isP("}")) {
            ImportSpec sp;
            sp.name = ident();
            if (eatKw("as")) sp.alias = propName();
            s->imports.push_back(sp);
            if (!eatP(",")) break;
          }
          expectP("}");
          if (isKw("from")) fail("re-exports are not supported");
          semi();
          return s;
        }
        if (isP("*")) fail("re-exports are not supported");
        Stmt* s = parseStatement(true);
        switch (s->k) {
          case SK::Var: case SK::Func: case SK::Class: case SK::Interface: case SK::TypeAlias: case SK::Enum: case SK::Declare:
            s->exported = true;
            break;
          default:
            failAt(l, "this declaration cannot be exported");
        }
        return s;
      }
      if (w == "import") {
        if (!topLevel) fail("import is only allowed at top level");
        if (peek().k == TK::Punct && (peek().text == "(" || peek().text == ".")) fail("dynamic import is not supported");
        return parseImport();
      }
      if (w == "let" || w == "const" || w == "var") {
        if (w == "const" && peek().k == TK::Ident && peek().text == "enum") {
          i++;
          Stmt* e = parseEnum();
          e->isConstEnum = true;
          return e;
        }
        if (w == "var") fail("'var' is not supported; use 'let' or 'const'");
        Stmt* s = parseVarDecl();
        semi();
        return s;
      }
      if (w == "function") return parseFunction();
      if (w == "async") {
        if (peek().k == TK::Ident && peek().text == "function") fail("async functions are not supported yet (reserved for coroutines); use a synchronous function (the host can run long work in slices with an instruction budget)");
      }
      if (w == "abstract" && peek().k == TK::Ident && peek().text == "class") fail("abstract classes are not supported");
      if (w == "class") return parseClass();
      if (w == "interface" && peek().k == TK::Ident) return parseInterface();
      if (w == "type" && peek().k == TK::Ident && !peek().nl) return parseTypeAlias();
      if (w == "enum") return parseEnum();
      if (w == "declare") return parseDeclare();
      if (w == "namespace" || w == "module") {
        if (peek().k == TK::Ident) fail("namespaces are not supported; use modules");
      }
      if (w == "if") return parseIf();
      if (w == "while") {
        i++;
        Stmt* s = A.stmt(SK::While, l);
        expectP("(");
        s->expr = parseExpr();
        expectP(")");
        s->body = parseStatement(false);
        return s;
      }
      if (w == "do") {
        i++;
        Stmt* s = A.stmt(SK::DoWhile, l);
        s->body = parseStatement(false);
        expectKw("while");
        expectP("(");
        s->expr = parseExpr();
        expectP(")");
        eatP(";");
        return s;
      }
      if (w == "for") return parseFor();
      if (w == "return") {
        i++;
        Stmt* s = A.stmt(SK::Return, l);
        if (!isP(";") && !isP("}") && !isEof() && !cur().nl) s->expr = parseExpr();
        semi();
        return s;
      }
      if (w == "break" || w == "continue") {
        i++;
        Stmt* s = A.stmt(w == "break" ? SK::Break : SK::Continue, l);
        if (cur().k == TK::Ident && !cur().nl && !isReserved(cur().text)) s->label = t[i++].text;
        semi();
        return s;
      }
      if (w == "throw") {
        i++;
        Stmt* s = A.stmt(SK::Throw, l);
        if (cur().nl) fail("newline not allowed after 'throw'");
        s->expr = parseExpr();
        semi();
        return s;
      }
      if (w == "try") fail("try/catch is not supported in E7 (errors cannot be caught); return an error value instead, e.g. a result object or null, and validate input before using it");
      if (w == "switch") return parseSwitch();
      if (w == "with") fail("'with' is not supported");
      if (w == "debugger") { i++; semi(); return A.stmt(SK::Empty, l); }
      if (peek().k == TK::Punct && peek().text == ":" && !isReserved(w)) {
        Stmt* s = A.stmt(SK::Labeled, l);
        s->label = t[i].text;
        i += 2;
        s->body = parseStatement(false);
        return s;
      }
    }
    Stmt* s = A.stmt(SK::Expr, l);
    s->expr = parseExpr();
    semi();
    return s;
  }

  Stmt* parseImport() {
    Loc l = loc();
    expectKw("import");
    Stmt* s = A.stmt(SK::Import, l);
    if (isKw("type")) i++;
    if (cur().k == TK::Str) {
      fail("side-effect imports are not supported");
    }
    if (eatP("*")) {
      expectKw("as");
      s->nsAlias = ident();
    } else if (isP("{")) {
      i++;
      while (!isP("}")) {
        if (isKw("type") && peek().k == TK::Ident && !(peek(1).text == "," || peek(1).text == "}" || peek(1).text == "as")) i++;
        ImportSpec sp;
        sp.name = propName();
        if (eatKw("as")) sp.alias = ident();
        else sp.alias = sp.name;
        s->imports.push_back(sp);
        if (!eatP(",")) break;
      }
      expectP("}");
    } else {
      fail("default imports are not supported; use import { name } from \"...\"");
    }
    expectKw("from");
    if (cur().k != TK::Str) fail("expected module path string");
    s->from = utf16ToUtf8(t[i++].str);
    semi();
    return s;
  }

  void parseParams(std::vector<Param>& params, bool allowProps) {
    expectP("(");
    while (!isP(")")) {
      Param pr;
      pr.loc = loc();
      if (allowProps) {
        while (isKw("public") || isKw("private") || isKw("protected") || isKw("readonly")) {
          if (peek().k == TK::Punct && (peek().text == ":" || peek().text == "," || peek().text == ")" || peek().text == "?" || peek().text == "=")) break;
          if (isKw("readonly")) pr.readonly = true;
          i++;
          pr.isProperty = true;
        }
      }
      if (eatP("...")) pr.rest = true;
      if (isKw("this")) fail("'this' parameters are not supported");
      if (isP("{") || isP("[")) {
        if (pr.isProperty || pr.rest) fail("invalid destructuring parameter");
        VarDecl d = parseBindingPattern();
        pr.pattern = A.decl(d);
        pr.name = "__arg" + std::to_string(params.size());
      } else {
        pr.name = ident();
      }
      if (eatP("?")) pr.optional = true;
      if (eatP(":")) pr.type = parseType();
      if (eatP("=")) pr.init = parseAssign();
      params.push_back(pr);
      if (!eatP(",")) break;
    }
    expectP(")");
  }

  void parseTypeParams(std::vector<std::string>& tps) {
    if (!isP("<")) return;
    i++;
    while (!isP(">")) {
      tps.push_back(ident());
      if (eatKw("extends")) parseType();
      if (eatP("=")) fail("default type parameters are not supported");
      if (!eatP(",")) break;
    }
    expectP(">");
  }

  Stmt* parseFunction() {
    Loc l = loc();
    expectKw("function");
    if (isP("*")) fail("generator functions are not supported yet (reserved for coroutines)");
    Stmt* s = A.stmt(SK::Func, l);
    s->name = ident();
    parseTypeParams(s->typeParams);
    parseParams(s->params, false);
    if (eatP(":")) s->retType = parseType();
    if (!isP("{")) fail("function overloads / declarations without body are not supported");
    s->body = parseBlock();
    Expr* none = nullptr;
    applyPatterns(s->params, s->body, none);
    return s;
  }

  Stmt* parseDeclare() {
    Loc l = loc();
    expectKw("declare");
    if (isKw("function")) {
      i++;
      Stmt* s = A.stmt(SK::Declare, l);
      s->declareKind = 1;
      s->name = ident();
      if (isP("<")) fail("generic native declarations are not supported");
      parseParams(s->params, false);
      expectP(":");
      s->retType = parseType();
      semi();
      return s;
    }
    if (isKw("const") || isKw("let") || isKw("var") || isKw("class") || isKw("module") || isKw("namespace") || isKw("global"))
      fail("only 'declare function' is supported (host native functions)");
    fail("unsupported declare statement");
  }

  Stmt* parseClass() {
    Loc l = loc();
    expectKw("class");
    Stmt* s = A.stmt(SK::Class, l);
    s->name = ident();
    parseTypeParams(s->typeParams);
    if (eatKw("extends")) s->extends = parsePrimaryType();
    if (eatKw("implements")) {
      do s->implements.push_back(parsePrimaryType());
      while (eatP(","));
    }
    expectP("{");
    while (!isP("}")) {
      if (eatP(";")) continue;
      ClassMember m;
      m.loc = loc();
      bool any = true;
      while (any) {
        any = false;
        if ((isKw("public") || isKw("private") || isKw("protected") || isKw("readonly") || isKw("static") || isKw("override") || isKw("declare")) &&
            peek().k != TK::Punct) {
          if (isKw("static")) m.isStatic = true;
          if (isKw("readonly")) m.readonly = true;
          if (isKw("declare")) fail("'declare' fields are not supported");
          i++;
          any = true;
        } else if ((isKw("public") || isKw("private") || isKw("protected") || isKw("readonly") || isKw("static") || isKw("override")) &&
                   peek().k == TK::Punct && peek().text == "[") {
          fail("computed members are not supported");
        }
      }
      if (isKw("abstract")) fail("abstract members are not supported");
      if ((isKw("get") || isKw("set")) && peek().k == TK::Ident) fail("getters and setters are not supported yet; use a plain field or methods such as getValue() / setValue(v)");
      if (isKw("async")) fail("async methods are not supported yet (reserved for coroutines); use a synchronous method (the host can run long work in slices with an instruction budget)");
      if (isP("*")) fail("generator methods are not supported");
      if (isP("[")) fail("computed members / index signatures are not supported");
      if (isKw("static") && peek().k == TK::Punct && peek().text == "{") fail("static blocks are not supported");
      if (isKw("constructor") && peek().k == TK::Punct && peek().text == "(") {
        i++;
        m.kind = ClassMember::Ctor;
        m.name = "constructor";
        if (m.isStatic) fail("constructor cannot be static");
        parseParams(m.params, true);
        m.body = parseBlock();
        {
          Expr* none = nullptr;
          applyPatterns(m.params, m.body, none);
        }
        s->members.push_back(m);
        continue;
      }
      m.name = propName();
      if (eatP("?")) m.optional = true;
      if (eatP("!")) {}
      if (isP("(") || isP("<")) {
        m.kind = ClassMember::Method;
        if (isP("<")) fail("generic methods are not supported yet; write one method per concrete type");
        parseParams(m.params, false);
        if (eatP(":")) m.retType = parseType();
        if (!isP("{")) fail("method overloads are not supported");
        m.body = parseBlock();
        {
          Expr* none = nullptr;
          applyPatterns(m.params, m.body, none);
        }
      } else {
        m.kind = ClassMember::Field;
        if (eatP(":")) m.type = parseType();
        if (eatP("=")) m.init = parseAssign();
        semi();
      }
      s->members.push_back(m);
    }
    expectP("}");
    return s;
  }

  Stmt* parseInterface() {
    Loc l = loc();
    expectKw("interface");
    Stmt* s = A.stmt(SK::Interface, l);
    s->name = ident();
    parseTypeParams(s->typeParams);
    if (eatKw("extends")) {
      do s->ifaceExtends.push_back(parsePrimaryType());
      while (eatP(","));
    }
    s->typeBody = parseObjectType();
    return s;
  }

  Stmt* parseTypeAlias() {
    Loc l = loc();
    expectKw("type");
    Stmt* s = A.stmt(SK::TypeAlias, l);
    s->name = ident();
    parseTypeParams(s->typeParams);
    expectP("=");
    s->typeBody = parseType();
    semi();
    return s;
  }

  Stmt* parseEnum() {
    Loc l = loc();
    expectKw("enum");
    Stmt* s = A.stmt(SK::Enum, l);
    s->name = ident();
    expectP("{");
    while (!isP("}")) {
      EnumMember m;
      m.name = propName();
      if (eatP("=")) m.init = parseAssign();
      s->enumMembers.push_back(m);
      if (!eatP(",")) break;
    }
    expectP("}");
    return s;
  }

  VarDecl parseBindingPattern() {
    VarDecl d;
    d.loc = loc();
    if (eatP("[")) {
      d.isArrayPat = true;
      while (!isP("]")) {
        if (isP(",")) { i++; d.elems.push_back(VarDecl()); continue; }
        if (isP("...")) fail("rest elements in destructuring are not supported");
        d.elems.push_back(parseBindingPattern());
        if (isP("=")) fail("default values in destructuring are not supported");
        if (!eatP(",")) break;
      }
      expectP("]");
    } else if (eatP("{")) {
      d.isObjPat = true;
      while (!isP("}")) {
        if (isP("...")) fail("rest properties are not supported");
        std::string key = propName();
        VarDecl sub;
        sub.loc = loc();
        if (eatP(":")) sub = parseBindingPattern();
        else sub.name = key;
        if (isP("=")) fail("default values in destructuring are not supported");
        d.keys.push_back(key);
        d.elems.push_back(sub);
        if (!eatP(",")) break;
      }
      expectP("}");
    } else {
      d.name = ident();
    }
    return d;
  }

  Stmt* parseVarDecl() {
    Loc l = loc();
    Stmt* s = A.stmt(SK::Var, l);
    s->isConst = cur().text == "const";
    i++;
    do {
      VarDecl d = parseBindingPattern();
      if (eatP("!")) {}
      if (eatP(":")) d.type = parseType();
      if (eatP("=")) d.init = parseAssign();
      s->decls.push_back(d);
    } while (eatP(","));
    return s;
  }

  Stmt* parseIf() {
    Loc l = loc();
    expectKw("if");
    Stmt* s = A.stmt(SK::If, l);
    expectP("(");
    s->expr = parseExpr();
    expectP(")");
    s->then = parseStatement(false);
    if (eatKw("else")) s->els = parseStatement(false);
    return s;
  }

  Stmt* parseFor() {
    Loc l = loc();
    expectKw("for");
    if (isKw("await")) fail("for await is not supported");
    expectP("(");
    Stmt* init = nullptr;
    if (isKw("let") || isKw("const") || isKw("var")) {
      if (isKw("var")) fail("'var' is not supported; use 'let' or 'const'");
      bool isConst = isKw("const");
      size_t save = i;
      i++;
      VarDecl d = parseBindingPattern();
      if (isKw("of") || isKw("in")) {
        bool isIn = isKw("in");
        i++;
        if (isIn) fail("for...in is not supported; use for...of over an array, a Map (for (const [k, v] of map)) or Object.keys(obj)");
        Stmt* s = A.stmt(SK::ForOf, l);
        s->isConst = isConst;
        s->decls.push_back(d);
        s->expr = parseAssign();
        expectP(")");
        s->body = parseStatement(false);
        return s;
      }
      i = save;
      noIn++;
      init = parseVarDecl();
      noIn--;
    } else if (!isP(";")) {
      Stmt* es = A.stmt(SK::Expr, loc());
      noIn++;
      es->expr = parseExpr();
      noIn--;
      if (isKw("of") || isKw("in")) fail("for...of requires a 'const' or 'let' declaration");
      init = es;
    }
    Stmt* s = A.stmt(SK::For, l);
    s->init = init;
    expectP(";");
    if (!isP(";")) s->expr = parseExpr();
    expectP(";");
    if (!isP(")")) s->update = parseExpr();
    expectP(")");
    s->body = parseStatement(false);
    return s;
  }

  Stmt* parseSwitch() {
    Loc l = loc();
    expectKw("switch");
    Stmt* s = A.stmt(SK::Switch, l);
    expectP("(");
    s->expr = parseExpr();
    expectP(")");
    expectP("{");
    while (!isP("}")) {
      SwitchCase c;
      if (eatKw("case")) {
        c.test = parseExpr();
      } else {
        expectKw("default");
      }
      expectP(":");
      while (!isKw("case") && !isKw("default") && !isP("}")) {
        if (isEof()) fail("unexpected end of file in switch");
        c.body.push_back(parseStatement(false));
      }
      s->cases.push_back(c);
    }
    expectP("}");
    return s;
  }

  // ---------------------------------------------------------------- expressions
  Expr* parseExpr() {
    Expr* e = parseAssign();
    if (isP(",")) fail("the comma operator is not supported");
    return e;
  }

  bool isAssignOp() const {
    if (cur().k != TK::Punct) return false;
    static const std::unordered_set<std::string> ops = {
        "=", "+=", "-=", "*=", "/=", "%=", "**=", "<<=", ">>=", ">>>=", "&=", "|=", "^=", "&&=", "||=", "??="};
    return ops.count(cur().text) > 0;
  }

  // Try parsing an arrow function at the current position.
  Expr* tryArrow() {
    Loc l = loc();
    if (isKw("async") && (peek().k == TK::Ident || (peek().k == TK::Punct && peek().text == "(")) && !peek().nl) {
      // async arrow
      size_t save = i;
      i++;
      Expr* e = tryArrow();
      if (e) fail("async functions are not supported yet (reserved for coroutines); use a synchronous function (the host can run long work in slices with an instruction budget)");
      i = save;
      return nullptr;
    }
    if (cur().k == TK::Ident && !isReserved(cur().text) && peek().k == TK::Punct && peek().text == "=>" && !peek().nl) {
      Expr* f = A.expr(EK::Arrow, l);
      Param p;
      p.loc = l;
      p.name = t[i].text;
      f->params.push_back(p);
      i += 2;
      parseArrowBody(f);
      return f;
    }
    if (isP("(") || isP("<")) {
      size_t save = i;
      try {
        Expr* f = A.expr(EK::Arrow, l);
        if (isP("<")) fail("generic arrow functions are not supported; write one function per concrete type");
        parseParams(f->params, false);
        if (eatP(":")) f->retType = parseType();
        if (!isP("=>") || cur().nl) { i = save; return nullptr; }
        i++;
        parseArrowBody(f);
        return f;
      } catch (ParseError&) {
        // decide: if the matching ')' is followed by '=>' this was really an arrow -> rethrow
        i = save;
        if (looksLikeFunctionType()) throw;
        return nullptr;
      }
    }
    return nullptr;
  }

  void parseArrowBody(Expr* f) {
    if (isP("{")) f->body = parseBlock();
    else {
      int saved = noIn;
      noIn = 0;
      f->exprBody = parseAssign();
      noIn = saved;
    }
    applyPatterns(f->params, f->body, f->exprBody);
  }

  // Destructuring parameters become `const <pattern> = __argN;` at the start of the body.
  void applyPatterns(std::vector<Param>& params, Stmt*& body, Expr*& exprBody) {
    std::vector<Stmt*> pre;
    for (auto& p : params) {
      if (!p.pattern) continue;
      Stmt* v = A.stmt(SK::Var, p.loc);
      v->isConst = true;
      VarDecl d = *p.pattern;
      Expr* id = A.expr(EK::Ident, p.loc);
      id->raw = p.name;
      d.init = id;
      v->decls.push_back(d);
      pre.push_back(v);
    }
    if (pre.empty()) return;
    if (exprBody) {
      Stmt* b = A.stmt(SK::Block, exprBody->loc);
      Stmt* r = A.stmt(SK::Return, exprBody->loc);
      r->expr = exprBody;
      b->stmts = pre;
      b->stmts.push_back(r);
      body = b;
      exprBody = nullptr;
      return;
    }
    if (body) body->stmts.insert(body->stmts.begin(), pre.begin(), pre.end());
  }

  Expr* parseAssign() {
    Nest guard(*this);
    if (Expr* arrow = tryArrow()) return arrow;
    if (isKw("yield")) fail("'yield' is not supported yet (reserved for coroutines)");
    Loc l = loc();
    Expr* lhs = parseConditional();
    if (isAssignOp()) {
      std::string op = t[i++].text;
      Expr* e = A.expr(EK::Assign, l);
      e->raw = op;
      e->a = lhs;
      e->b = parseAssign();
      return e;
    }
    return lhs;
  }

  Expr* parseConditional() {
    Loc l = loc();
    Expr* c = parseBinary(0);
    if (isP("?")) {
      i++;
      Expr* e = A.expr(EK::Cond, l);
      e->a = c;
      int saved = noIn;
      noIn = 0;
      e->b = parseAssign();
      noIn = saved;
      expectP(":");
      e->c = parseAssign();
      return e;
    }
    return c;
  }

  int binPrec(const Tok& tk) const {
    if (tk.k == TK::Ident) {
      if (tk.text == "instanceof") return 7;
      if (tk.text == "in") return noIn ? -1 : 7;
      if (tk.text == "as" || tk.text == "satisfies") return tk.nl ? -1 : 7;
      return -1;
    }
    if (tk.k != TK::Punct) return -1;
    const std::string& o = tk.text;
    if (o == "??") return 1;
    if (o == "||") return 2;
    if (o == "&&") return 3;
    if (o == "|") return 4;
    if (o == "^") return 5;
    if (o == "&") return 6;
    if (o == "==" || o == "!=" || o == "===" || o == "!==") return 6 + 0;  // adjusted below
    if (o == "<" || o == ">" || o == "<=" || o == ">=") return 7;
    if (o == "<<" || o == ">>" || o == ">>>") return 8;
    if (o == "+" || o == "-") return 9;
    if (o == "*" || o == "/" || o == "%") return 10;
    if (o == "**") return 11;
    return -1;
  }
  // precedence table with equality between & and relational
  int prec(const Tok& tk) const {
    int p = binPrec(tk);
    if (p < 0) return p;
    if (tk.k == TK::Punct && (tk.text == "==" || tk.text == "!=" || tk.text == "===" || tk.text == "!==")) return 65;
    // scale: ?? 10, || 20, && 30, | 40, ^ 50, & 60, == 65, rel 70, shift 80, add 90, mul 100, ** 110
    return p * 10;
  }

  Expr* parseBinary(int minPrec) {
    Loc l = loc();
    Expr* lhs = parseUnary();
    while (true) {
      int p = prec(cur());
      if (p < 0 || p < minPrec) break;
      Tok op = t[i];
      if (op.k == TK::Ident && (op.text == "as" || op.text == "satisfies")) {
        i++;
        if (isKw("const")) fail("'as const' is not supported");
        Expr* e = A.expr(EK::As, l);
        e->a = lhs;
        e->asType = parseType();
        if (op.text == "satisfies") e->raw = "satisfies";
        lhs = e;
        continue;
      }
      if (op.k == TK::Ident && op.text == "in") fail("the 'in' operator is not supported");
      i++;
      bool rightAssoc = op.text == "**";
      Expr* rhs = parseBinary(rightAssoc ? p : p + 1);
      Expr* e;
      if (op.text == "&&" || op.text == "||" || op.text == "??") e = A.expr(EK::Logical, l);
      else e = A.expr(EK::Binary, l);
      e->raw = op.text;
      e->a = lhs;
      e->b = rhs;
      if (op.text == "**" && lhs->k == EK::Unary) failAt(l, "unary operator before '**' requires parentheses");
      // mixing ?? with || / && without parens is a syntax error in JS
      if (op.text == "??" && ((lhs->k == EK::Logical && lhs->raw != "??" ) || (rhs->k == EK::Logical && rhs->raw != "??")))
        failAt(l, "'??' cannot be mixed with '||' or '&&' without parentheses");
      lhs = e;
    }
    return lhs;
  }

  Expr* parseUnary() {
    Nest guard(*this);
    Loc l = loc();
    if (cur().k == TK::Punct) {
      const std::string& o = cur().text;
      if (o == "!" || o == "-" || o == "+" || o == "~") {
        i++;
        Expr* e = A.expr(EK::Unary, l);
        e->raw = o;
        e->a = parseUnary();
        return e;
      }
      if (o == "++" || o == "--") {
        i++;
        Expr* e = A.expr(EK::Update, l);
        e->raw = o;
        e->prefix = true;
        e->a = parseUnary();
        return e;
      }
      if (o == "<") fail("angle-bracket type assertions are not supported; use 'as'");
    }
    if (cur().k == TK::Ident) {
      const std::string& w = cur().text;
      if (w == "typeof") {
        i++;
        Expr* e = A.expr(EK::TypeOf, l);
        e->a = parseUnary();
        return e;
      }
      if (w == "void") fail("the 'void' operator is not supported");
      if (w == "delete") fail("'delete' is not supported; use Map for dynamic keys");
      if (w == "await") fail("'await' is not supported yet (reserved for coroutines); use a synchronous function (the host can run long work in slices with an instruction budget)");
    }
    return parsePostfix();
  }

  Expr* parsePostfix() {
    Loc l = loc();
    Expr* e = parseCallMember();
    if (cur().k == TK::Punct && (cur().text == "++" || cur().text == "--") && !cur().nl) {
      Expr* u = A.expr(EK::Update, l);
      u->raw = t[i++].text;
      u->prefix = false;
      u->a = e;
      return u;
    }
    return e;
  }

  void parseArgs(std::vector<Expr*>& args) {
    expectP("(");
    int saved = noIn;
    noIn = 0;
    while (!isP(")")) {
      if (isP("...")) {
        Loc l = loc();
        i++;
        Expr* sp = A.expr(EK::Spread, l);
        sp->a = parseAssign();
        args.push_back(sp);
      } else {
        args.push_back(parseAssign());
      }
      if (!eatP(",")) break;
    }
    noIn = saved;
    expectP(")");
  }

  // speculative type arguments before '(' : f<T>(...)
  bool tryTypeArgs(std::vector<TypeNode*>& out) {
    if (!isP("<")) return false;
    size_t save = i;
    try {
      i++;
      std::vector<TypeNode*> args;
      while (!isP(">")) {
        args.push_back(parseType());
        if (!eatP(",")) break;
      }
      if (!isP(">")) { i = save; return false; }
      i++;
      if (!isP("(")) { i = save; return false; }
      out = args;
      return true;
    } catch (ParseError&) {
      i = save;
      return false;
    }
  }

  Expr* parseCallMember() {
    Loc l = loc();
    Expr* e;
    if (isKw("new")) {
      i++;
      if (isP(".")) fail("new.target is not supported");
      Expr* n = A.expr(EK::New, l);
      // callee: identifier path
      Loc cl = loc();
      Expr* callee = A.expr(EK::Ident, cl);
      callee->raw = ident();
      while (isP(".")) {
        i++;
        Expr* m = A.expr(EK::Member, cl);
        m->a = callee;
        m->raw = propName();
        callee = m;
      }
      n->a = callee;
      if (isP("<")) {
        i++;
        while (!isP(">")) {
          n->typeArgs.push_back(parseType());
          if (!eatP(",")) break;
        }
        closeAngle();
      }
      if (isP("(")) parseArgs(n->list);
      e = n;
    } else {
      e = parsePrimary();
    }
    while (true) {
      Loc ml = loc();
      if (isP(".")) {
        i++;
        if (cur().k == TK::PrivateName) fail("private names (#x) are not supported; use 'private'");
        Expr* m = A.expr(EK::Member, ml);
        m->a = e;
        m->raw = propName();
        e = m;
      } else if (isP("?.")) {
        i++;
        if (isP("(")) {
          Expr* c = A.expr(EK::Call, ml);
          c->a = e;
          c->optional = true;
          parseArgs(c->list);
          e = c;
        } else if (isP("[")) {
          i++;
          Expr* ix = A.expr(EK::Index, ml);
          ix->a = e;
          ix->optional = true;
          ix->b = parseExpr();
          expectP("]");
          e = ix;
        } else {
          Expr* m = A.expr(EK::Member, ml);
          m->a = e;
          m->optional = true;
          m->raw = propName();
          e = m;
        }
      } else if (isP("[")) {
        i++;
        Expr* ix = A.expr(EK::Index, ml);
        ix->a = e;
        int saved = noIn;
        noIn = 0;
        ix->b = parseExpr();
        noIn = saved;
        expectP("]");
        e = ix;
      } else if (isP("(")) {
        Expr* c = A.expr(EK::Call, ml);
        c->a = e;
        parseArgs(c->list);
        e = c;
      } else if (isP("<") && !cur().nl) {
        std::vector<TypeNode*> targs;
        if (!tryTypeArgs(targs)) break;
        Expr* c = A.expr(EK::Call, ml);
        c->a = e;
        c->typeArgs = targs;
        parseArgs(c->list);
        e = c;
      } else if (isP("!") && !cur().nl) {
        // non-null assertion (not followed by '=' which would be '!=' lexed separately)
        i++;
        Expr* nn = A.expr(EK::NonNull, ml);
        nn->a = e;
        e = nn;
      } else if (cur().k == TK::TmplFull || cur().k == TK::TmplHead) {
        fail("tagged templates are not supported");
      } else {
        break;
      }
    }
    return e;
  }

  Expr* parseTemplate() {
    Loc l = loc();
    Expr* e = A.expr(EK::Template, l);
    if (cur().k == TK::TmplFull) {
      e->quasis.push_back(t[i++].str);
      return e;
    }
    e->quasis.push_back(t[i++].str);  // head
    while (true) {
      int saved = noIn;
      noIn = 0;
      e->list.push_back(parseExpr());
      noIn = saved;
      if (cur().k == TK::TmplMid) { e->quasis.push_back(t[i++].str); continue; }
      if (cur().k == TK::TmplTail) { e->quasis.push_back(t[i++].str); break; }
      fail("expected '}' in template literal");
    }
    return e;
  }

  Expr* parsePrimary() {
    Loc l = loc();
    const Tok& tk = cur();
    switch (tk.k) {
      case TK::Num: {
        Expr* e = A.expr(EK::Num, l);
        e->num = tk.num;
        e->isIntLit = tk.isInt;
        e->raw = tk.text;
        i++;
        return e;
      }
      case TK::BigInt: {
        Expr* e = A.expr(EK::BigLit, l);
        e->raw = tk.text;
        i++;
        return e;
      }
      case TK::Str: {
        Expr* e = A.expr(EK::Str, l);
        e->str = tk.str;
        i++;
        return e;
      }
      case TK::TmplFull:
      case TK::TmplHead:
        return parseTemplate();
      case TK::Ident: {
        const std::string& w = tk.text;
        if (w == "true" || w == "false") { Expr* e = A.expr(EK::Bool, l); e->bval = w == "true"; i++; return e; }
        if (w == "null") { i++; return A.expr(EK::Null, l); }
        if (w == "undefined") { i++; return A.expr(EK::Undefined, l); }
        if (w == "this") { i++; return A.expr(EK::This, l); }
        if (w == "super") { i++; return A.expr(EK::Super, l); }
        if (w == "function") {
          // function expression -> arrow-like closure (no own 'this')
          i++;
          if (isP("*")) fail("generator functions are not supported");
          Expr* f = A.expr(EK::Arrow, l);
          if (cur().k == TK::Ident && !isP("(")) f->raw = ident();
          parseParams(f->params, false);
          if (eatP(":")) f->retType = parseType();
          f->body = parseBlock();
          applyPatterns(f->params, f->body, f->exprBody);
          f->intrinsic = 1;  // function expression
          return f;
        }
        if (w == "class") fail("class expressions are not supported");
        Expr* e = A.expr(EK::Ident, l);
        e->raw = ident();
        return e;
      }
      case TK::Punct: {
        if (tk.text == "(") {
          i++;
          int saved = noIn;
          noIn = 0;
          Expr* inner = parseExpr();
          noIn = saved;
          expectP(")");
          Expr* p = A.expr(EK::Paren, l);
          p->a = inner;
          return p;
        }
        if (tk.text == "[") {
          i++;
          Expr* e = A.expr(EK::Array, l);
          int saved = noIn;
          noIn = 0;
          while (!isP("]")) {
            if (isP(",")) fail("array holes are not supported");
            if (isP("...")) {
              Loc sl = loc();
              i++;
              Expr* sp = A.expr(EK::Spread, sl);
              sp->a = parseAssign();
              e->list.push_back(sp);
            } else {
              e->list.push_back(parseAssign());
            }
            if (!eatP(",")) break;
          }
          noIn = saved;
          expectP("]");
          return e;
        }
        if (tk.text == "{") {
          i++;
          Expr* e = A.expr(EK::Object, l);
          int saved = noIn;
          noIn = 0;
          while (!isP("}")) {
            ObjProp pr;
            pr.loc = loc();
            if (isP("...")) {
              i++;
              pr.spread = true;
              pr.value = parseAssign();
              e->props.push_back(pr);
              if (!eatP(",")) break;
              continue;
            }
            if (isP("[")) fail("computed property names are not supported");
            if ((isKw("get") || isKw("set") || isKw("async")) && peek().k == TK::Ident) fail("accessor / async properties are not supported; use a plain property or a method");
            bool wasIdent = cur().k == TK::Ident;
            pr.key = propName();
            if (isP("(")) fail("method shorthand in object literals is not supported; use 'name: (args) => ...'");
            if (eatP(":")) {
              pr.value = parseAssign();
            } else {
              if (!wasIdent) fail("expected ':'");
              pr.shorthand = true;
              Expr* id = A.expr(EK::Ident, pr.loc);
              id->raw = pr.key;
              pr.value = id;
            }
            e->props.push_back(pr);
            if (!eatP(",")) break;
          }
          noIn = saved;
          expectP("}");
          return e;
        }
        if (tk.text == "/" || tk.text == "/=") fail("regular expressions are not supported; use string methods such as indexOf, includes, startsWith, split or replaceAll");
        break;
      }
      default:
        break;
    }
    fail("unexpected '" + describe(tk) + "'");
  }
};

}  // namespace

// Iteratively measures AST depth (the checker and code generator recurse over the tree).
static bool checkDepth(SourceFile& file, std::vector<CompileError>& errs) {
  const int kMax = 400;
  struct Item { const void* node; bool isStmt; int d; };
  std::vector<Item> st;
  for (Stmt* s : file.body) st.push_back({s, true, 1});
  while (!st.empty()) {
    Item it = st.back();
    st.pop_back();
    if (!it.node) continue;
    if (it.d > kMax) {
      Loc l = it.isStmt ? static_cast<const Stmt*>(it.node)->loc : static_cast<const Expr*>(it.node)->loc;
      errs.push_back(CompileError{file.path, l, "expression or statement nesting is too deep (limit 400)"});
      return false;
    }
    int d = it.d + 1;
    if (it.isStmt) {
      const Stmt* s = static_cast<const Stmt*>(it.node);
      for (const Stmt* x : s->stmts) st.push_back({x, true, d});
      for (const Stmt* x : {s->body, s->then, s->els, s->init}) st.push_back({x, true, d});
      st.push_back({s->expr, false, d});
      st.push_back({s->update, false, d});
      for (auto& v : s->decls) st.push_back({v.init, false, d});
      for (auto& p : s->params) st.push_back({p.init, false, d});
      for (auto& c : s->cases) {
        st.push_back({c.test, false, d});
        for (const Stmt* x : c.body) st.push_back({x, true, d});
      }
      for (auto& m : s->members) {
        st.push_back({m.init, false, d});
        st.push_back({m.body, true, d});
        for (auto& p : m.params) st.push_back({p.init, false, d});
      }
      for (auto& em : s->enumMembers) st.push_back({em.init, false, d});
    } else {
      const Expr* e = static_cast<const Expr*>(it.node);
      for (const Expr* x : {e->a, e->b, e->c, e->exprBody}) st.push_back({x, false, d});
      for (const Expr* x : e->list) st.push_back({x, false, d});
      for (auto& p : e->props) st.push_back({p.value, false, d});
      for (auto& p : e->params) st.push_back({p.init, false, d});
      st.push_back({e->body, true, d});
    }
  }
  return true;
}

bool parseSource(SourceFile& file, Arena& arena, std::vector<CompileError>& errs) {
  try {
    Lexer lx(file.text);
    std::vector<Tok> toks = lx.run();
    Parser p(file, arena, std::move(toks));
    p.parseProgram();
    return checkDepth(file, errs);
  } catch (ParseError& e) {
    errs.push_back(CompileError{file.path, e.loc, e.msg});
    return false;
  }
}

}  // namespace e7
